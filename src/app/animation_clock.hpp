#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace catpulse {
struct Rhythm {
    bool music=false;
    double pulse=0,period=0,nextPulse=0;
};
// Loops are anchored to emitted pulses. A slow musical pulse permits one
// explicit halfway subdivision; no prediction can extend beyond that beat.
// Intermediate frames use absolute QPC time, never accumulated tick counts.
class AnimationClock {
    double anchor=0,end=0,consumedPulse=0;
    int frame=0,loops=1,loop=0;
    bool parked=false,finishing=true;
public:
    explicit AnimationClock(double now):anchor(now),end(now+1000){}
    int update(double now,const Rhythm& rhythm){
        bool fresh=rhythm.music&&std::isfinite(rhythm.pulse)&&std::isfinite(rhythm.period)&&
            rhythm.period>=80&&rhythm.period<=12000&&rhythm.pulse>consumedPulse+.1&&
            rhythm.pulse<=now+2&&now-rhythm.pulse<rhythm.period;
        if(fresh){
            consumedPulse=rhythm.pulse;anchor=rhythm.pulse;end=anchor+rhythm.period;
            if(loops==1&&rhythm.period>=600)loops=2;
            else if(loops==2&&rhythm.period<=540)loops=1;
            frame=0;loop=0;parked=false;finishing=false;
        }
        if(!rhythm.music)finishing=true;
        if(!parked&&!finishing&&rhythm.music&&rhythm.nextPulse>now&&rhythm.nextPulse>anchor+80){
            // The schedule can already point to the next beat while frame 10
            // waits for its emitted event. Never begin another loop here.
            if(now<end-2)end=rhythm.nextPulse;
        }
        if(!parked){
            double duration=std::max(80.,end-anchor)/loops;
            if(loop+1<loops&&!finishing&&now>=anchor+duration*(loop+1)){
                if(rhythm.music&&rhythm.nextPulse>now&&now<end){++loop;frame=0;}
                else finishing=true;
            }
            double localStart=anchor+duration*loop;
            int position=int(std::floor(std::max(0.,now-localStart)*10/duration));
            frame=std::max(frame,std::clamp(position,0,9));
            if(frame==9&&finishing)parked=true;
        }
        return frame;
    }
    double nextFrameTime()const{
        if(parked)return std::numeric_limits<double>::infinity();
        double duration=(end-anchor)/loops;
        if(frame==9){
            if(!finishing&&loop+1<loops)return anchor+duration*(loop+1);
            return std::numeric_limits<double>::infinity();
        }
        return anchor+duration*(loop+(frame+1)/10.);
    }
    bool stopped()const{return parked;}
    bool finishingLoop()const{return finishing;}
    int loopsPerPulse()const{return loops;}
};
}
