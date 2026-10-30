#pragma once
#include "beatnet.hpp"
#include "predictor.hpp"
#include "scheduler.hpp"
#include "local_tempo.hpp"

namespace kp {
class BeatClock {
    Shared& state;PulseScheduler scheduler;SampleRing<131072> ring;
    HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr),dataEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    std::thread worker;size_t untilWake=0;std::atomic<uint64_t> sourceBase{0};
    std::mutex configuration;double requestedBpm=0,requestedRate=1;bool requestedLocal=false,requestedWarmTempo=false;
    void run(){
        try{
            std::unique_ptr<BeatNetwork> network;std::unique_ptr<BeatFeatures> features;BeatPeaks peaks;
            AnalysisResampler resampler;PhaseClock motor;LocalTempoEstimator local;
            int generation=-1,relearn=-1,tempo=-1;uint64_t processed=0;double streamOffset=0;
            auto clear=[&](){
                auto start=ring.discard();processed=0;
                if(features)features->reset();if(network)network->reset();peaks.reset();motor.reset();local.reset();
                int rate=std::max(8000,state.rate.load());streamOffset=std::max(0.,(double(start)-double(sourceBase.load()))*1000/rate);
                resampler.reset(rate);state.trackingState=0;state.learningProgress=state.trackingConfidence=0;
                state.beatPhaseLocked=false;state.phaseProvisional=false;state.nextBeatQpcMs=0;state.phaseAgreement=0;state.phaseEvidence=0;scheduler.update({},generation,relearn,tempo);
            };
            HANDLE events[]={stopEvent,dataEvent};
            for(;;){
                DWORD wait=WaitForMultipleObjects(2,events,FALSE,INFINITE);if(wait==WAIT_OBJECT_0)break;
                if(wait==WAIT_FAILED)throw std::runtime_error("Fallo al esperar el audio del reloj");
                double bpm=0,rate=1;int version;bool estimate=false,warmTempo=false;
                {std::lock_guard<std::mutex> lock(configuration);bpm=requestedBpm;rate=requestedRate;estimate=requestedLocal;warmTempo=requestedWarmTempo;version=state.tempoGeneration.load();}
                if(estimate&&state.localTempoReady.load())bpm=state.localTempoBpm.load();
                if(!state.beatEnabled.load()||(!estimate&&effectiveTempo(bpm,rate)<=0)){
                    ring.discard();scheduler.update({},state.generation.load(),state.relearn.load(),version);
                    features.reset();network.reset();motor.setTempoBpm(0);state.beatModelState=0;continue;
                }
                if(!network){network=std::make_unique<BeatNetwork>(state.modelDirectory+L"\\beatnet.onnx");features=std::make_unique<BeatFeatures>();generation=-1;state.beatModelState=1;}
                if(generation!=state.generation.load()||relearn!=state.relearn.load()||tempo!=version){
                    bool keep=warmTempo&&generation==state.generation.load()&&relearn==state.relearn.load();
                    generation=state.generation.load();relearn=state.relearn.load();tempo=version;
                    if(keep){scheduler.update({},generation,relearn,tempo);local.reset();}
                    else clear();
                    motor.setTempoBpm(effectiveTempo(bpm,rate),keep);}
                if(!state.running.load()||nowMs()-state.lastPacket.load()>300){motor.suspend();scheduler.update({},generation,relearn,tempo);continue;}
                if(ring.size()>size_t(std::max(8000,state.rate.load()))/3){++state.overflows;clear();continue;}
                float sample;
                bool music=state.transportPlaying.load()&&state.bMusic.load()&&state.classifiedGeneration.load()==state.generation.load();
                while(ring.pop(sample)){
                    if(!state.beatEnabled.load()||tempo!=state.tempoGeneration.load()||generation!=state.generation.load())break;
                    ++processed;
                    resampler.push(sample,[&](float v){features->push(v,[&](auto frame,int64_t index,float rms){
                        auto p=music&&rms>1e-6f?network->process(frame):std::array<float,3>{};state.beatScore=p[0]+p[1];
                        peaks.push(p[0]+p[1],streamOffset+index*20.,[&](double stamp,float confidence){motor.beat(stamp,confidence);if(estimate)local.observe(stamp,confidence);});
                    });});
                }
                if(generation!=state.generation.load()||relearn!=state.relearn.load()||tempo!=state.tempoGeneration.load()||!state.beatEnabled||!state.running)continue;
                double audioTime=streamOffset+processed*1000./std::max(8000,state.rate.load());
                bool active=nowMs()-state.lastAudible.load()<130;

                if(music&&active&&estimate){
                    auto measured=local.advance(audioTime);
                    if(measured.ready&&measured.updated){
                        bool refining=state.localTempoReady.load();bpm=measured.bpm/rate;
                        if(refining){motor.refineTempoBpm(measured.bpm,audioTime);++state.localTempoRefinements;}
                        else{motor.setTempoBpm(measured.bpm,true);state.pulseDivision=musicalPulseDivision(measured.bpm,PulseMode(state.pulseMode.load()));}
                        state.localTempoBpm=bpm;state.catalogBpm=bpm;state.effectiveBpm=measured.bpm;state.beatInterval=beatIntervalMs(measured.bpm);state.localTempoConfidence=measured.confidence;state.localTempoReady=true;state.notify();
                    }
                }
                if(music&&active)motor.advance(audioTime,true,true);else motor.suspend();
                int division=estimate?refinedPulseDivision(effectiveTempo(bpm,rate),PulseMode(state.pulseMode.load()),state.pulseDivision.load()):musicalPulseDivision(effectiveTempo(bpm,rate),PulseMode(state.pulseMode.load()));state.pulseDivision=division;
                auto plan=motor.outputPlan(division);state.trackingState=int(motor.status());state.trackingConfidence=motor.confidence();state.learningProgress=motor.progress(audioTime);
                state.phaseCorrections=motor.phaseCorrections();state.phaseAgreement=motor.phaseAgreement();state.phaseEvidence=int(motor.phaseEvidence());
                double origin=state.captureOrigin.load();if(origin){plan.origin+=origin;plan.expires+=origin;}else plan.valid=false;
                state.pulseIntervalMs=beatIntervalMs(effectiveTempo(bpm,rate))*division;
                double next=motor.nextBeat(origin?std::max(audioTime,nowMs()-origin):audioTime);
                state.beatPhaseLocked=motor.phaseLocked();state.phaseProvisional=motor.phaseProvisional();state.nextBeatQpcMs=origin&&std::isfinite(next)?origin+next:0;
                scheduler.update(plan,generation,relearn,tempo);state.sampleAge=std::max(0.,nowMs()-(origin+audioTime));
            }
            scheduler.update({},generation,relearn,tempo);
        }catch(const std::exception& e){state.beatEnabled=false;scheduler.update({},-1,-1,-1);state.beatModelState=-1;state.trackingState=0;state.beatPhaseLocked=false;state.nextBeatQpcMs=0;std::lock_guard<std::mutex> lock(state.textMutex);state.beatError=wide(e.what());state.notify();}
    }
public:
    explicit BeatClock(Shared& s):state(s),scheduler(s){worker=std::thread([this]{run();});}
    ~BeatClock(){SetEvent(stopEvent);if(worker.joinable())worker.join();CloseHandle(stopEvent);CloseHandle(dataEvent);}
    void setTempo(double bpm,double rate=1){
        double effective=effectiveTempo(bpm,rate);if(effective<=0){bpm=0;rate=1;}
        std::lock_guard<std::mutex> lock(configuration);
        if(!requestedLocal&&std::abs(bpm-requestedBpm)<1e-8&&std::abs(rate-requestedRate)<1e-8&&state.beatEnabled.load()==(effective>0))return;
        requestedWarmTempo=requestedLocal&&effective>0;
        state.beatEnabled=false;int version=++state.tempoGeneration;
        requestedLocal=false;state.localTempoMode=false;state.localTempoReady=false;state.localTempoBpm=0;
        state.localTempoRefinements=0;state.localTempoConfidence=0;
        requestedBpm=bpm;requestedRate=rate;state.catalogBpm=bpm;state.playbackRate=rate;state.effectiveBpm=effective;state.beatInterval=beatIntervalMs(effective);
        int division=musicalPulseDivision(effective,PulseMode(state.pulseMode.load()));state.pulseDivision=division;state.pulseIntervalMs=beatIntervalMs(effective)*division;
        state.lastHit=0;state.trackingConfidence=0;state.learningProgress=0;state.beatPhaseLocked=false;state.phaseProvisional=false;state.nextBeatQpcMs=0;
        state.trackingState=effective>0?int(TrackingState::Learning):int(TrackingState::Waiting);
        state.beatEnabled=effective>0;scheduler.update({},state.generation.load(),state.relearn.load(),version);SetEvent(dataEvent);
    }
    void useLocalTempo(double rate=1){
        std::lock_guard<std::mutex> lock(configuration);if(requestedLocal&&std::abs(requestedRate-rate)<1e-8)return;
        state.beatEnabled=false;int version=++state.tempoGeneration;requestedBpm=0;requestedRate=rate;requestedLocal=true;requestedWarmTempo=false;
        state.localTempoMode=true;state.localTempoReady=false;state.localTempoBpm=0;state.catalogBpm=0;state.effectiveBpm=0;state.beatInterval=0;state.pulseIntervalMs=0;state.playbackRate=rate;
        state.localTempoRefinements=0;state.localTempoConfidence=0;
        state.beatPhaseLocked=false;state.phaseProvisional=false;state.lastHit=0;state.trackingState=int(TrackingState::Learning);state.beatEnabled=true;
        scheduler.update({},state.generation.load(),state.relearn.load(),version);SetEvent(dataEvent);
    }
    void beginSource(){sourceBase=ring.produced();untilWake=0;}
    void push(float sample){if(!ring.push(sample))++state.overflows;size_t divisor=state.beatEnabled.load()?50:10;if(++untilWake>=size_t(std::max(8000,state.rate.load()))/divisor){untilWake=0;SetEvent(dataEvent);}}
};
}
