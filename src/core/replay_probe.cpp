#include "engine.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>
using namespace kp;
int wmain(int argc,wchar_t** argv){
    if(argc<5)return 2;
    try{
        std::ifstream input(argv[1],std::ios::binary);char tag[4];uint32_t bytes;
        input.read(tag,4);input.read(reinterpret_cast<char*>(&bytes),4);input.read(tag,4);
        uint16_t format=0,channels=0,bits=0;uint32_t rate=0;std::vector<char> data;
        while(input.read(tag,4)&&input.read(reinterpret_cast<char*>(&bytes),4)){
            std::vector<char> chunk(bytes);input.read(chunk.data(),bytes);if(bytes%2)input.ignore();
            if(!memcmp(tag,"fmt ",4)&&bytes>=16){memcpy(&format,chunk.data(),2);memcpy(&channels,chunk.data()+2,2);memcpy(&rate,chunk.data()+4,4);memcpy(&bits,chunk.data()+14,2);}
            if(!memcmp(tag,"data",4))data=std::move(chunk);
        }
        if(format!=1||bits!=16||channels!=1)throw std::runtime_error("Replay requires mono PCM16 WAV");
        std::ofstream log(argv[4]);log<<std::setprecision(12)<<"type,seconds,weight,aux\n";
        BeatNetwork net(std::wstring(argv[2])+L"\\beatnet.onnx");BeatFeatures feat;BeatPeaks peaks;AnalysisResampler beat;beat.reset(rate);
        PhaseClock motor;double requestedBpm=std::wcstod(argv[3],nullptr);motor.setTempoBpm(requestedBpm);double last=-1e9;
        bool localMode=requestedBpm<=0,tempoReady=!localMode;LocalTempoEstimator localTempo;int division=1;
        PulseMode pulseMode=argc>5?PulseMode(_wtoi(argv[5])):PulseMode::Auto;
        auto pcm=reinterpret_cast<const int16_t*>(data.data());size_t count=data.size()/2;
        for(size_t i=0;i<count;++i){float sample=pcm[i]/32768.f;
            beat.push(sample,[&](float v){feat.push(v,[&](auto f,int64_t frame,float rms){std::array<float,3> p{};if(rms>1e-6f)p=net.process(f);peaks.push(p[0]+p[1],frame*20.,[&](double t,float confidence){motor.beat(t,confidence);if(localMode)localTempo.observe(t,confidence);log<<"beat,"<<t/1000<<','<<confidence<<",0\n";});});});
            if(i%std::max(1u,rate/100)==0){double t=i*1000./rate;
                if(localMode){auto estimated=localTempo.advance(t);if(estimated.ready&&estimated.updated){
                    if(tempoReady)motor.refineTempoBpm(estimated.bpm,t);else{motor.setTempoBpm(estimated.bpm,true);division=musicalPulseDivision(estimated.bpm,pulseMode);}
                    log<<(tempoReady?"tempo_update,":"tempo,")<<t/1000<<','<<estimated.bpm<<','<<estimated.confidence<<'\n';tempoReady=true;}}
                motor.advance(t,true,true);
                division=localMode?refinedPulseDivision(motor.tempoBpm(),pulseMode,division):musicalPulseDivision(motor.tempoBpm(),pulseMode);
                auto plan=motor.outputPlan(division);
                if(i%std::max(1u,rate/5)==0)log<<"state,"<<t/1000<<','<<motor.confidence()<<','<<int(motor.status())<<'\n';
                double due=plan.next(std::max(t-35,last+120));if(std::isfinite(due)&&due<=t){last=due;log<<"pulse,"<<due/1000<<','<<plan.confidence<<','<<int(motor.status())<<'\n';}}
        }
        std::cout<<"REPLAY_FINISHED\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
