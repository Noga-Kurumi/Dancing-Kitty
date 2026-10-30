#include "predictor.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace kp;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    try{
        check(!validCatalogBpm(0)&&!validCatalogBpm(-1)&&!validCatalogBpm(INFINITY),"Invalid BPM accepted");
        check(beatIntervalMs(effectiveTempo(0))==0,"Missing BPM creates a clock");
        check(std::abs(beatIntervalMs(effectiveTempo(127.2))-25000./53)<1e-9,"Fractional BPM was rounded");
        check(std::abs(beatIntervalMs(effectiveTempo(120,1.25))-400)<1e-9,"Playback speed ignored");
        check(musicalPulseDivision(159.6,PulseMode::Auto)==2&&musicalPulseDivision(127.2,PulseMode::Auto)==1,"Automatic clap rate is wrong");
        check(musicalPulseDivision(159.6,PulseMode::EveryBeat)==1&&musicalPulseDivision(100,PulseMode::HalfTime)==2,"Manual pulse mode ignored");
        check(refinedPulseDivision(140.2,PulseMode::Auto,1)==1&&refinedPulseDivision(139.8,PulseMode::Auto,2)==2,"Small refinements toggle half-time");
        check(refinedPulseDivision(145,PulseMode::Auto,1)==2&&refinedPulseDivision(135,PulseMode::Auto,2)==1,"Clap rate hysteresis blocks a sustained change");
        PulsePlan half;half.valid=true;half.origin=123;half.period=beatIntervalMs(159.6)*2;half.expires=100000;
        check(std::abs((half.next(123+half.period+.01)-half.next(123))-half.period)<1e-8,"Half-time contains extra/missing beats");
        PhaseClock motor;motor.setTempoBpm(120);double last=-1e9;std::vector<double> pulses;
        for(int t=0;t<12000;t+=10){
            for(int k=0;k<30;++k){double stamp=230+k*500.;if(t==stamp+80&&(k<10||k>14))motor.beat(stamp,.95f);}
            if(t==4690)motor.beat(4517,.9f);
            motor.advance(t,true,true);const auto& plan=motor.prediction();double next=plan.next(std::max(double(t)-.001,last+120));
            if(std::isfinite(next)&&next<t+10){pulses.push_back(next);last=next;}
            if(motor.phaseLocked())check(std::abs(motor.beatPeriod()-500)<1e-8,"Audio changed catalogue BPM");
        }
        if(pulses.size()<16){std::cerr<<"pulse count "<<pulses.size()<<":";for(auto value:pulses)std::cerr<<' '<<value;std::cerr<<'\n';}
        check(pulses.size()>=16,"Missing drum observations suppressed musical pulses");
        check(pulses.front()<1000,"An artificial learning countdown delays the first pulse");
        for(size_t i=0;i<pulses.size();++i){
            check(std::abs(std::remainder(pulses[i]-230,500.))<2,"Offbeat attack shifted phase");
            if(i)check(std::abs(pulses[i]-pulses[i-1]-500)<2,"Musical grid contains holes or extra beats");
        }
        motor.advance(12000,false,true);check(!motor.phaseLocked()&&!motor.prediction().valid,"Non-music retains a schedule");
        motor.setTempoBpm(90);check(!motor.phaseLocked(),"Tempo change retains phase");motor.reset();check(motor.tempoBpm()==90,"Reset forgot catalogue BPM");
        motor.setTempoBpm(0);for(int t=0;t<7000;t+=10){if(t%500==0)motor.beat(t,.9f);motor.advance(t,true,true);}
        check(!motor.prediction().valid,"Audio inferred a tempo without catalogue BPM");
        PhaseClock sparse;sparse.setTempoBpm(120);
        for(int t=0;t<7000;t+=10){if(t>=250&&(t-250)%1000==0)sparse.beat(t,.9f);sparse.advance(t,true,true);}
        check(sparse.phaseLocked()&&sparse.prediction().valid&&std::abs(sparse.beatPeriod()-500)<1e-8,"Sparse drums replaced the known tempo");
        auto slow=sparse.outputPlan(2);check(std::abs(slow.period-1000)<1e-8&&std::abs(std::remainder(slow.next(7000)-250,1000.))<3,"Half-time chose the empty positions between strong beats");
        auto previousEvidence=sparse.phaseEvidence();sparse.suspend();
        check(!sparse.phaseLocked()&&!sparse.prediction().valid&&sparse.tempoBpm()==120,"Buffering discards BPM or leaves output active");
        sparse.advance(7000,true,true);check(sparse.phaseLocked()&&sparse.phaseEvidence()>=previousEvidence,"Short buffering discarded phase history");
        sparse.advance(7000,true,false);check(!sparse.prediction().valid,"Silence keeps the clock alive");
        PhaseClock intro;intro.setTempoBpm(120);
        for(int t=0;t<8000;t+=10){if(t%370==0)intro.beat(t,.32f);intro.advance(t,true,true);check(!intro.phaseLocked(),"Weak intro invents a beat");}
        for(int t=8000;t<13000;t+=10){if((t-8230)%500==80&&t>=8310)intro.beat(t-80,.95f);intro.advance(t,true,true);}
        check(intro.phaseLocked(),"Strong rhythm after intro cannot acquire phase");
        PhaseClock shifting;shifting.setTempoBpm(120);
        for(int t=0;t<17000;t+=10){
            double onset=t<8500?230:430;
            if(t>=onset+80&&int(t-onset-80)%500==0)shifting.beat(t-80,.95f);
            shifting.advance(t,true,true);
        }
        if(!shifting.phaseLocked()||!shifting.phaseCorrections())std::cerr<<"shift lock="<<shifting.phaseLocked()<<" provisional="<<shifting.phaseProvisional()<<" corrections="<<shifting.phaseCorrections()<<" next="<<shifting.nextBeat(17000)<<" agreement="<<shifting.phaseAgreement()<<'\n';
        check(shifting.phaseLocked()&&shifting.phaseCorrections()>0,"Rolling window cannot replace an incorrect phase");
        check(std::abs(std::remainder(shifting.nextBeat(17000)-430,500.))<3,"Correction kept the old phase");
        check(std::abs(shifting.beatPeriod()-500)<1e-8,"Phase correction changed BPM");
        PhaseClock subdivisions;subdivisions.setTempoBpm(80);double previousPulse=-1e9;int subdivisionPulses=0;
        for(int t=0;t<24000;t+=5){
            if(t>=310&&(t-310)%750==0)subdivisions.beat(t-80,.95f);
            if(t>=8185&&(t-8185)%750==0)subdivisions.beat(t-80,.9f);
            subdivisions.advance(t,true,true);
            if(t>12000)check(subdivisions.phaseLocked(),"Regular subdivisions invalidate a half-time clock");
            double due=subdivisions.prediction().next(std::max(double(t)-35,previousPulse+120));
            if(due<=t){if(previousPulse>0)check(std::abs(due-previousPulse-750)<3,"Subdivision flips phase or omits a pulse");previousPulse=due;++subdivisionPulses;}
        }
        check(subdivisionPulses>=30&&subdivisions.phaseCorrections()==0,"Subdivisions keep reacquiring phase");
        auto retained=shifting.phaseEvidence();auto beforeNext=shifting.nextBeat(17000);
        shifting.refineTempoBpm(120.12,17000);
        check(shifting.phaseLocked()&&shifting.phaseEvidence()==retained,"Local refinement discards phase history");
        check(std::abs(shifting.nextBeat(17000)-beforeNext)<1,"Local refinement jumps the next beat");
        shifting.refineTempoBpm(120,17000);
        for(int t=17000;t<22000;t+=10)shifting.advance(t,true,true);
        check(!shifting.phaseLocked(),"Stale rhythm evidence invents beats indefinitely");
        PulsePlan fractional;fractional.valid=true;fractional.origin=137;fractional.period=beatIntervalMs(127.2);fractional.expires=1e9;
        check(std::abs(fractional.next(137)-(137+25000./53))<1e-8,"Initial fractional beat is wrong");
        check(std::abs(fractional.next(137+fractional.period*100000-.01)-(137+100000.*25000./53))<1e-5,"Timer intervals accumulate drift");
        fractional.expires=100;check(!std::isfinite(fractional.next(137)),"Expired plan emits a pulse");
        PlaybackPositionTracker position;
        check(!position.observe(0,100000,1)&&!position.observe(1000,101000,1),"Normal progress treated as a seek");
        check(position.observe(30000,102000,1)&&position.observe(5000,103000,1),"Seek did not invalidate alignment");
        position.reset();check(!position.observe(0,104000,1.25)&&!position.observe(1250,105000,1.25),"Playback rate breaks timeline tracking");
        std::cout<<"PASS fixed BPM + sliding phase: weak intro, acquisition, phase replacement, bounded holdover, sparse beats, outliers, speed and seek\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
