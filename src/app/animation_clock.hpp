#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace catpulse {
struct Rhythm {
    bool music=false;
    double pulse=0,period=0,nextPulse=0;
};
// A 25 fps stroke finishes on frame 10 and waits. Only a shorter beat interval
// compresses the stroke. Predictions never start a new animation by themselves.
class AnimationClock {
    static constexpr double NormalDuration=400;
    double anchor=0,duration=NormalDuration,consumedPulse=0;
    int frame=0;
    bool parked=false,finishing=true;
public:
    explicit AnimationClock(double now):anchor(now){}
    int update(double now,const Rhythm& rhythm){
        bool fresh=rhythm.music&&std::isfinite(rhythm.pulse)&&std::isfinite(rhythm.period)&&
            rhythm.period>=80&&rhythm.period<=12000&&rhythm.pulse>consumedPulse+.1&&
            rhythm.pulse<=now+2&&now-rhythm.pulse<rhythm.period;
        if(fresh){
            consumedPulse=rhythm.pulse;anchor=rhythm.pulse;
            duration=std::min(NormalDuration,rhythm.period);frame=0;parked=false;finishing=false;
        }
        if(!rhythm.music)finishing=true;
        if(!parked){
            if(!finishing&&rhythm.nextPulse>now&&rhythm.nextPulse>anchor+80)
                duration=std::min(duration,rhythm.nextPulse-anchor);
            frame=std::max(frame,std::clamp(int(std::floor(std::max(0.,now-anchor)*10/duration)),0,9));
            if(frame==9)parked=true;
        }
        return frame;
    }
    double nextFrameTime()const{
        return parked?std::numeric_limits<double>::infinity():anchor+duration*(frame+1)/10.;
    }
    bool stopped()const{return parked;}
    bool finishingLoop()const{return finishing;}
};
}
