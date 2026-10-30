#pragma once
#include "dsp.hpp"
#include "tracking_state.hpp"
#include "tempo.hpp"
#include <deque>
#include <limits>

namespace kp {
struct RhythmObservation {double time=0;float weight=0;};
struct PulsePlan {
    bool valid=false;double origin=0,period=0,expires=0;float confidence=0;
    double next(double after)const{
        if(!valid||period<=0)return std::numeric_limits<double>::infinity();
        double time=origin+(std::floor((after-origin)/period)+1)*period;
        return time<=expires?time:std::numeric_limits<double>::infinity();
    }
};

// Fixed catalogue BPM; only phase is estimated. Weighted circular averaging
// handles the wrap at the end of a beat. Bounded rolling history permits a new
// phase to replace an incorrect intro alignment without changing the period.
class PhaseClock {
    std::deque<RhythmObservation> beats;
    double bpm=0,period=0,origin=0,started=-1,lastUpdate=-1e9;
    double candidate=0,candidateSince=-1,lastSupported=-1e9;
    bool acquired=false,confirmed=false,everLocked=false;float quality=0,agreement=0,learning=0;
    uint64_t corrections=0;size_t evidence=0;
    int outputDivision=1,outputSlot=0;
    TrackingState mode=TrackingState::Waiting;PulsePlan plan;
    struct Fit {double phase=0,quality=0,residual=1e9,agreement=0,first=0,last=-1e9,mean=0;size_t support=0,strong=0;};
    static double error(double value,double cycle){return value-std::round(value/cycle)*cycle;}
    Fit fitAt(double anchor,double now,double window)const{
        Fit result;double total=0,support=0,c=0,s=0,probability=0,subdivision=0;size_t count=0,strong=0,subCount=0,subStrong=0;
        const double tolerance=std::min(55.,period*.12);
        int64_t first=INT64_MAX,last=INT64_MIN,subFirst=INT64_MAX,subLast=INT64_MIN;double firstTime=now,lastTime=-1e9;
        for(const auto& observation:beats){
            if(observation.time<now-window)continue;
            // Recent observations carry more weight, but cannot authorize a
            // replacement phase without repeated support and persistence.
            double w=observation.weight*observation.weight*std::exp((observation.time-now)/4000.);
            total+=w;double d=error(observation.time-anchor,period);
            if(std::abs(d)>tolerance){
                // Repeated halfway observations are subdivisions of this
                // musical grid, not votes against it. Require a second
                // coherent series before discounting these observations.
                if(std::abs(error(observation.time-anchor-period*.5,period))<=tolerance){
                    subdivision+=w;++subCount;if(observation.weight>=.55f)++subStrong;
                    auto index=int64_t(std::llround((observation.time-anchor-period*.5)/period));subFirst=std::min(subFirst,index);subLast=std::max(subLast,index);
                }
                continue;
            }
            support+=w;probability+=observation.weight*w;++count;if(observation.weight>=.55f)++strong;
            double angle=2*pi*d/period;c+=w*std::cos(angle);s+=w*std::sin(angle);
            auto index=int64_t(std::llround((observation.time-anchor)/period));first=std::min(first,index);last=std::max(last,index);
            firstTime=std::min(firstTime,observation.time);lastTime=std::max(lastTime,observation.time);
        }
        if(count<4||strong<3||support<=0||total<=0||last-first<3)return result;
        double phase=anchor+std::atan2(s,c)*period/(2*pi),residual=0;
        for(const auto& observation:beats){
            if(observation.time<now-window)continue;double d=std::abs(error(observation.time-phase,period));
            if(d<=tolerance)residual+=d*observation.weight*observation.weight*std::exp((observation.time-now)/4000.);
        }
        if(subCount>=4&&subStrong>=3&&subLast-subFirst>=3)total-=subdivision;
        residual/=support;double ratio=std::min(1.,support/total),coverage=std::min(1.,double(count)/double(last-first+1));
        double coherence=std::max(0.,1-residual/tolerance);
        result={phase,.45*ratio+.35*coherence+.20*coverage,residual,ratio,firstTime,lastTime,probability/support,count,strong};return result;
    }
    Fit bestFit(double time,double window)const{
        Fit best;for(const auto& observation:beats){if(observation.time<time-window||observation.weight<.35f)continue;
            auto fit=fitAt(observation.time,time,window);if(fit.quality>best.quality)best=fit;}return best;
    }
    bool reliable(const Fit& fit,double time)const{
        return fit.quality>=.80&&fit.agreement>=.65&&fit.residual<=std::min(30.,period*.08)&&fit.mean>=.55&&
            time-fit.last<std::max(1000.,period*2.6)&&fit.support>=4&&fit.strong>=3;
    }
public:
    void reset(){beats.clear();origin=0;started=-1;lastUpdate=-1e9;candidate=candidateSince=-1;lastSupported=-1e9;acquired=confirmed=everLocked=false;quality=agreement=learning=0;evidence=0;outputDivision=1;outputSlot=0;mode=TrackingState::Waiting;plan={};}
    void setTempoBpm(double value){double next=beatIntervalMs(value)>0?value:0;if(std::abs(next-bpm)>1e-8){bpm=next;period=beatIntervalMs(bpm);reset();}}
    void refineTempoBpm(double value,double time){
        double next=beatIntervalMs(value);if(next<=0||!std::isfinite(time))return;
        if(period<=0){setTempoBpm(value);return;}
        // Preserve the last grid point, rolling history and beat numbering.
        double index=std::floor((time-origin)/period),pivot=origin+index*period;
        origin=pivot-index*next;bpm=value;period=next;candidateSince=-1;
        if(plan.valid){plan.origin=origin;plan.period=period;}
    }
    double tempoBpm()const{return bpm;}
    void beat(double time,float weight){if(beats.empty()||time>beats.back().time+100)beats.push_back({time,std::clamp(weight,.05f,1.f)});}
    void advance(double time,bool music,bool active){
        if(period<=0||!music||!active){reset();return;}if(started<0)started=time;
        const double history=std::max(8000.,period*6),tolerance=std::min(55.,period*.12);
        while(!beats.empty()&&beats.front().time<time-history)beats.pop_front();
        if(time-lastUpdate<200)return;lastUpdate=time;
        auto best=bestFit(time,history);auto recent=bestFit(time,std::max(4000.,period*4));
        if(reliable(recent,time)&&recent.agreement>=.80&&recent.quality>best.quality+.035)best=recent;
        auto current=acquired?fitAt(origin,time,history):Fit{};
        bool good=reliable(best,time);quality=float(best.quality);agreement=float(best.agreement);evidence=best.support;
        bool currentGood=acquired&&reliable(current,time);
        bool displaced=acquired&&good&&std::abs(error(best.phase-origin,period))>tolerance&&(!currentGood||best.quality>current.quality+.08);
        if(currentGood){lastSupported=current.last;origin+=std::clamp(error(current.phase-origin,period)*.20,-3.,3.);confirmed=true;everLocked=true;}
        if(acquired&&time-lastSupported>std::max(3500.,period*4)){acquired=confirmed=false;plan={};candidateSince=-1;}
        if(good&&(!acquired||displaced)){
            confirmed=false;
            if(candidateSince<0||std::abs(error(best.phase-candidate,period))>tolerance){candidate=best.phase;candidateSince=time;}
            else candidate+=error(best.phase-candidate,period)*.35;
            if(!acquired||time-candidateSince>=600){
                double next=(acquired||everLocked)?origin+error(best.phase-origin,period):best.phase;
                if(everLocked&&std::abs(next-origin)>10)++corrections;
                origin=next;acquired=confirmed=true;lastSupported=best.last;everLocked=true;candidateSince=-1;
            }
        }else if(!displaced)candidateSince=-1;
        // No artificial learning countdown: the first usable observation seeds
        // a provisional clock immediately. The sliding window confirms or
        // replaces that phase later; BPM remains fixed throughout.
        if(!acquired){
            for(auto it=beats.rbegin();it!=beats.rend();++it){
                if(it->weight>=.55f&&time-it->time<std::max(1000.,period*2.6)){
                    if(everLocked&&std::abs(error(it->time-origin,period))>tolerance)++corrections;
                    origin=it->time;lastSupported=it->time;acquired=true;confirmed=false;break;
                }
            }
        }
        learning=confirmed?1:float(std::min(1.,best.support/4.));
        if(!acquired){plan={};mode=everLocked?TrackingState::Recovering:TrackingState::Learning;return;}
        plan={true,origin,period,time+std::max(1500.,period*2),quality};
        mode=confirmed?TrackingState::Synchronized:everLocked?TrackingState::Recovering:TrackingState::Learning;
    }
    const PulsePlan& prediction()const{return plan;}
    void suspend(){plan={};mode=TrackingState::Waiting;}
    PulsePlan outputPlan(int division){
        PulsePlan result=plan;if(!plan.valid||division<=1){outputDivision=1;outputSlot=0;return result;}
        division=std::clamp(division,1,16);std::array<double,16> weights{};std::array<int,16> votes{};
        for(const auto& observation:beats){
            double index=std::round((observation.time-origin)/period);
            if(std::abs(observation.time-(origin+index*period))>std::min(55.,period*.12))continue;
            int slot=int((int64_t(index)%division+division)%division);
            weights[slot]+=observation.weight*observation.weight;votes[slot]++;
        }
        int best=int(std::max_element(weights.begin(),weights.begin()+division)-weights.begin());
        if(outputDivision!=division){outputDivision=division;outputSlot=best;}
        else if(votes[best]>=3&&weights[best]>weights[outputSlot]*1.5)outputSlot=best;
        result.origin+=outputSlot*period;result.period*=division;return result;
    }
    TrackingState status()const{return mode;}
    float confidence()const{return quality;}
    float progress(double)const{return learning;}
    double beatPeriod()const{return period;}
    bool phaseLocked()const{return acquired&&confirmed&&mode!=TrackingState::Waiting;}
    bool phaseProvisional()const{return acquired&&!confirmed&&mode!=TrackingState::Waiting;}
    double nextBeat(double after)const{return plan.next(after);}
    uint64_t phaseCorrections()const{return corrections;}
    float phaseAgreement()const{return agreement;}
    size_t phaseEvidence()const{return evidence;}
};
}
