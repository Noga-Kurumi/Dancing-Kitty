#pragma once
#include "common.hpp"
#include "predictor.hpp"
#include <thread>

namespace kp {
class PulseScheduler {
    Shared& state;std::mutex mutex;
    PulsePlan plan;int generation=-1,revision=-1,tempoRevision=-1;
    HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr),updateEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr),timer=nullptr;
    std::thread worker;
    void run(){
        HANDLE waits[]={stopEvent,updateEvent,timer};double last=-1e9;
        for(;;){
            PulsePlan current;int token,learn,tempo;
            {std::lock_guard<std::mutex> lock(mutex);current=plan;token=generation;learn=revision;tempo=tempoRevision;}
            double now=nowMs();constexpr double offset=0;
            bool allowed=current.valid&&std::abs(current.period-state.pulseIntervalMs.load())<.01&&state.transportPlaying&&state.beatEnabled&&state.running&&state.bMusic&&state.classifiedGeneration.load()==state.generation.load()&&now-state.lastPacket.load()<180&&now-state.lastAudible.load()<130&&token==state.generation.load()&&learn==state.relearn.load()&&tempo==state.tempoGeneration.load();
            // A plan update just after a timer deadline may still deliver that
            // one recent pulse. Never replay an older backlog or duplicate it.
            double due=allowed?current.next(std::max(now-offset-35,last-offset+120))+offset:std::numeric_limits<double>::infinity();
            if(!std::isfinite(due)){CancelWaitableTimer(timer);if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)==WAIT_OBJECT_0)break;continue;}
            LARGE_INTEGER relative;relative.QuadPart=-std::max<LONGLONG>(1,LONGLONG((due-now)*10000));
            if(!SetWaitableTimer(timer,&relative,0,nullptr,nullptr,FALSE))break;
            DWORD result=WaitForMultipleObjects(3,waits,FALSE,INFINITE);
            if(result==WAIT_OBJECT_0)break;if(result!=WAIT_OBJECT_0+2)continue;
            now=nowMs();
            // A changed plan may cancel or move an impact while its timer waits.
            {std::lock_guard<std::mutex> lock(mutex);
                double stillDue=plan.next(due-offset-2)+offset;
                allowed=plan.valid&&generation==token&&revision==learn&&tempoRevision==tempo&&std::abs(stillDue-due)<12;}
            allowed=allowed&&std::abs(current.period-state.pulseIntervalMs.load())<.01&&state.transportPlaying&&state.beatEnabled&&state.running&&state.bMusic&&state.classifiedGeneration.load()==state.generation.load()&&token==state.generation.load()&&learn==state.relearn.load()&&tempo==state.tempoGeneration.load()&&now-state.lastAudible.load()<130&&now-state.lastPacket.load()<180;
            if(!allowed||now-due>35){++state.skippedPulses;continue;}
            last=due;state.lastHit=due;state.schedulerErrorMs=now-due;
            state.maxSchedulerErrorMs=std::max(state.maxSchedulerErrorMs.load(),std::abs(now-due));++state.pulses;
            state.notifyClient();
        }
    }
public:
    explicit PulseScheduler(Shared& s):state(s){
        timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(!timer)timer=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        if(!stopEvent||!updateEvent||!timer)throw std::runtime_error("No se pudo crear el reloj predictivo");
        worker=std::thread([this]{run();});
    }
    ~PulseScheduler(){SetEvent(stopEvent);if(worker.joinable())worker.join();CloseHandle(timer);CloseHandle(stopEvent);CloseHandle(updateEvent);}
    void update(const PulsePlan& value,int token,int learn,int tempo){
        {std::lock_guard<std::mutex> lock(mutex);plan=value;generation=token;revision=learn;tempoRevision=tempo;}SetEvent(updateEvent);
        bool changed=false;
        {std::lock_guard<std::mutex> lock(state.planMutex);const auto& previous=state.publishedPlan;
            changed=previous.valid!=value.valid||std::abs(previous.period-value.period)>.01||
                (value.valid&&value.period>0&&std::abs(std::remainder(value.origin-previous.origin,value.period))>1);
            state.publishedPlan={value.valid,value.origin,value.period,value.expires};}
        if(changed)state.notifyClient();
    }
};
}
