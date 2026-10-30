#pragma once
#include "common.hpp"
#include "dsp.hpp"
#include <onnxruntime_cxx_api.h>
#include <thread>

namespace kp {
class MusicClassifier {
    Shared& state;
    SampleRing<131072> ring;
    HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE dataEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    std::thread worker;
    size_t untilWake=0;
    void run() {
        try {
            Ort::Env env(ORT_LOGGING_LEVEL_ERROR,"KickPulse");
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(1); options.SetInterOpNumThreads(1);
            options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
            options.AddConfigEntry("session.intra_op.allow_spinning","0");
            options.AddConfigEntry("session.inter_op.allow_spinning","0");
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            auto path=state.modelDirectory+L"\\yamnet.onnx";
            Ort::Session session(env,path.c_str(),options);
            Ort::AllocatorWithDefaultOptions allocator;
            auto inputName=session.GetInputNameAllocated(0,allocator);
            auto outputName=session.GetOutputNameAllocated(0,allocator);
            const char* inputs[]={inputName.get()}; const char* outputs[]={outputName.get()};
            constexpr size_t window=15600,hop=7680;
            std::array<float,window> history{},wave{};
            size_t pos=0,valid=0,since=0;
            std::array<int64_t,1> shape{window};
            auto mem=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
            auto tensor=Ort::Value::CreateTensor<float>(mem,wave.data(),wave.size(),shape.data(),shape.size());
            MusicDecision decision;
            int generation=state.generation.load();
            state.modelState=1;
            HANDLE events[]={stopEvent,dataEvent};
            for(;;) {
                if(WaitForMultipleObjects(2,events,FALSE,200)==WAIT_OBJECT_0) break;
                if(generation!=state.generation.load()) {
                    generation=state.generation.load(); ring.discard(); pos=valid=since=0;
                    decision.reset(); state.bMusic=false;
                }
                if(!state.running.load()||nowMs()-state.lastPacket.load()>700||nowMs()-state.lastAudible.load()>180) {
                    bool wasMusic=state.bMusic.load();
                    decision.reset(); state.bMusic=false;
                    state.classifiedGeneration=generation;if(wasMusic)state.notify();
                    pos=valid=since=0; ring.discard(); continue;
                }
                float sample;
                while(ring.pop(sample)) {
                    history[pos]=sample; pos=(pos+1)%window;
                    valid=std::min(window,valid+1); ++since;
                    if(valid<window || since<hop) continue;
                    since=0;
                    double power=0;
                    for(size_t i=0;i<window;++i) { wave[i]=history[(pos+i)%window]; power+=double(wave[i])*wave[i]; }
                    bool silent=power/window<1e-7;
                    float music=0;
                    if(!silent) {
                        auto result=session.Run(Ort::RunOptions{nullptr},inputs,&tensor,1,outputs,1);
                        const auto info=result[0].GetTensorTypeAndShapeInfo();
                        size_t count=info.GetElementCount();
                        if(count<521 || count%521!=0) throw std::runtime_error("Salida YAMNet incompatible");
                        const float* scores=result[0].GetTensorData<float>();
                        for(size_t i=132;i<count;i+=521)music+=scores[i]/float(count/521);
                    }
                    // Do not publish a result from an old device/capture session.
                    if(generation!=state.generation.load() || !state.running.load()) break;
                    ++state.classifierWindows;state.bMusic=decision.update(music,silent);state.classifiedGeneration=generation;state.musicUpdated=nowMs();state.notify();
                }
            }
        } catch(const std::exception& e) {
            state.bMusic=false; state.modelState=-1;
            std::lock_guard<std::mutex> lock(state.textMutex); state.modelError=wide(e.what());
            state.notify();
        }
    }
public:
    explicit MusicClassifier(Shared& s):state(s) {
        worker=std::thread([this]{run();});
    }
    ~MusicClassifier() { SetEvent(stopEvent); if(worker.joinable()) worker.join(); CloseHandle(stopEvent); CloseHandle(dataEvent); }
    void reset() { ++state.generation;state.classifiedGeneration=-1;state.bMusic=false;state.musicUpdated=0;SetEvent(dataEvent);state.notify(); }
    void push(float sample) {
        if(!ring.push(sample)) ++state.overflows;
        if(++untilWake>=7680) { untilWake=0; SetEvent(dataEvent); }
    }
};
}
