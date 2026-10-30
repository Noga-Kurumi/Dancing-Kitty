#include "beat_engine.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

double qpcMs(){LARGE_INTEGER f,t;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t);return t.QuadPart*1000./f.QuadPart;}
std::string escaped(const std::wstring& text){
    int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);std::string utf8(size_t(n),'\0');
    if(n)WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),utf8.data(),n,nullptr,nullptr);
    std::string result="\"";for(unsigned char c:utf8){if(c=='"'||c=='\\'){result+='\\';result+=char(c);}else if(c<32){const char* hex="0123456789abcdef";result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}else result+=char(c);}return result+'"';
}
uint64_t cpuTime(){FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);ULARGE_INTEGER a{},b{};a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return a.QuadPart+b.QuadPart;}
int wmain(int argc,wchar_t** argv){
    if(argc<4){std::cerr<<"Usage: IntegrationProbe MODELS_DIR OUTPUT_JSONL SECONDS\n";return 2;}
    try{
        std::ofstream log(argv[2],std::ios::trunc);if(!log)throw std::runtime_error("Cannot create diagnostic output");
        const double duration=std::wcstod(argv[3],nullptr);kp::BeatEngine engine(argv[1],argc>4&&std::wstring(argv[4])==L"--offline");engine.start();
        double start=qpcMs(),previous=start,cpu=0;uint64_t previousCpu=cpuTime();
        DWORD logical=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        log<<std::boolalpha<<std::setprecision(12);
        std::cout<<"PROBE_READY\n"<<std::flush;
        while(qpcMs()-start<duration*1000){
            double now=qpcMs();auto state=engine.status();
            if(now-previous>=1000){auto used=cpuTime();cpu=(used-previousCpu)*.0001/(now-previous)*100;previousCpu=used;previous=now;}
            PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
            log<<"{\"elapsed\":"<<(now-start)/1000<<",\"qpc_ms\":"<<now<<",\"playing\":"<<state.playerPlaying<<",\"capturing\":"<<state.capturing<<",\"bMusic\":"<<state.bMusic
               <<",\"lookup\":"<<int(state.lookupState)<<",\"clock\":"<<state.beatClockEnabled<<",\"phase\":"<<state.beatPhaseLocked
               <<",\"bpm\":"<<state.catalog.bpm<<",\"period_ms\":"<<state.beatIntervalMs<<",\"next_beat_qpc_ms\":"<<state.nextBeatQpcMs<<",\"pulses\":"<<state.beatCount<<",\"last_pulse_qpc_ms\":"<<state.lastBeatQpcMs
               <<",\"pulse_bpm\":"<<state.pulseBpm<<",\"pulse_division\":"<<state.pulseDivision<<",\"pulse_period_ms\":"<<state.pulseIntervalMs<<",\"tracking\":"<<int(state.state)<<",\"phase_corrections\":"<<state.phaseCorrections<<",\"phase_agreement\":"<<state.phaseAgreement<<",\"phase_evidence\":"<<state.phaseEvidence
               <<",\"tempo_refinements\":"<<state.localTempoRefinements<<",\"tempo_confidence\":"<<state.localTempoConfidence
               <<",\"tempo_revision\":"<<state.tempoRevision<<",\"capture_generation\":"<<state.captureGeneration<<",\"model\":"<<state.beatModelState<<",\"requests\":"<<state.catalogRequests<<",\"classifier_windows\":"<<state.classifierWindows
               <<",\"overflows\":"<<state.overflows<<",\"discontinuities\":"<<state.discontinuities<<",\"audio_age_ms\":"<<state.audioAgeMs<<",\"timer_error_ms\":"<<state.schedulerErrorMs<<",\"timer_max_error_ms\":"<<state.maxSchedulerErrorMs
               <<",\"cpu_one_core_percent\":"<<cpu<<",\"cpu_total_percent\":"<<cpu/std::max<DWORD>(1,logical)<<",\"ram_mb\":"<<memory.WorkingSetSize/1048576.
               <<",\"title\":"<<escaped(state.track.title)<<",\"artist\":"<<escaped(state.track.artist)<<",\"app\":"<<escaped(state.playerAppId)<<",\"reason\":"<<escaped(state.catalog.reason)<<",\"error\":"<<escaped(state.error)<<"}\n";
            log.flush();std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        engine.stop();std::cout<<"PROBE_FINISHED\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
