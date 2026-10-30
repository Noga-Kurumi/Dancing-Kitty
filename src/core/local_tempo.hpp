#pragma once
#include "dsp.hpp"
#include <deque>
#include <vector>
namespace kp {
struct LocalTempoResult {bool ready=false;double bpm=0;float confidence=0;bool updated=false;};
class LocalTempoEstimator {
    struct Observation {double time;float weight;};std::deque<Observation> events;
    double lastUpdate=-1e9,candidate=0,since=-1,lastEvidence=-1e9;LocalTempoResult result;
    // Bounded robust regression of beat number against time, reusing the
    // existing model observations. No additional audio model or timer.
    double refine(double period,double now)const{
        double anchor=0,bestWeight=0;const double tolerance=std::min(70.,period*.16);
        for(const auto& seed:events){double support=0;
            for(const auto& event:events){double d=std::remainder(event.time-seed.time,period);
                if(std::abs(d)<=tolerance)support+=event.weight*event.weight*std::exp((event.time-now)/8000.);}
            if(support>bestWeight){bestWeight=support;anchor=seed.time;}}
        double slope=period,intercept=0;size_t count=0;double first=now,last=-1e9;
        for(int pass=0;pass<3;++pass){double sw=0,sx=0,sy=0,sxx=0,sxy=0;count=0;first=now;last=-1e9;
            for(const auto& event:events){double y=event.time-anchor,x=std::round(y/period);
                if(std::abs(y-x*period)>tolerance)continue;
                double residual=std::abs(y-(intercept+x*slope));
                double w=event.weight*event.weight*std::exp((event.time-now)/8000.);
                if(pass)w*=std::min(1.,std::min(18.,period*.04)/std::max(1.,residual));
                sw+=w;sx+=w*x;sy+=w*y;sxx+=w*x*x;sxy+=w*x*y;++count;
                first=std::min(first,event.time);last=std::max(last,event.time);
            }
            double denominator=sw*sxx-sx*sx;if(count<8||last-first<4000||denominator<=0)return 0;
            slope=(sw*sxy-sx*sy)/denominator;intercept=(sy-slope*sx)/sw;
        }
        return std::abs(slope-period)<=period*.04?slope:0;
    }
public:
    void reset(){events.clear();lastUpdate=lastEvidence=-1e9;candidate=0;since=-1;result={};}
    void observe(double time,float weight){
        if(!std::isfinite(time)||!std::isfinite(weight)||weight<.45f)return;
        if(events.empty()||time>events.back().time+100)events.push_back({time,std::min(weight,1.f)});
        while(events.size()>96)events.pop_front();
    }
    LocalTempoResult advance(double now){
        result.updated=false;
        while(!events.empty()&&events.front().time<now-12000)events.pop_front();
        if(now-lastUpdate<(result.ready?1000:200)||events.size()<8||now-events.back().time>1500||events.back().time<=lastEvidence)return result;
        lastUpdate=now;lastEvidence=events.back().time;
        double best=0,bestScore=0;std::vector<double> candidates;
        for(size_t i=1;i<events.size();++i)for(size_t j=i>4?i-4:0;j<i;++j){double delta=events[i].time-events[j].time;
            for(int n=1;n<=4;++n){double p=delta/n;if(p<250||p>1500)continue;
                bool duplicate=false;for(auto old:candidates)if(std::abs(old-p)<5){duplicate=true;break;}if(!duplicate)candidates.push_back(p);}}
        for(double p:candidates){
            double total=0,support=0,direct=0,sum=0,normalizer=0,residual=0;size_t count=0;
            for(size_t i=1;i<events.size();++i){double d=events[i].time-events[i-1].time,w=std::min(events[i].weight,events[i-1].weight);total+=w;
                int n=int(std::llround(d/p));if(n<1||n>4||std::abs(d-n*p)>std::min(55.,p*.10))continue;
                support+=w;if(n==1)direct+=w;sum+=w*n*d;normalizer+=w*n*n;residual+=w*std::abs(d/n-p);++count;
            }
            if(count<6||support<total*.80||direct<total*.55||normalizer<=0)continue;
            double score=.65*support/total+.35*direct/total-.25*(residual/support)/std::min(55.,p*.10);
            if(score>bestScore){
                double sw=0,sx=0,sy=0,sxx=0,sxy=0;double anchor=events.back().time;
                for(const auto& event:events){double index=std::round((event.time-anchor)/p),y=event.time-anchor;
                    if(std::abs(y-index*p)>std::min(55.,p*.10))continue;
                    double w=event.weight;sw+=w;sx+=w*index;sy+=w*y;sxx+=w*index*index;sxy+=w*index*y;}
                double denominator=sw*sxx-sx*sx;
                bestScore=score;best=denominator>0?(sw*sxy-sx*sy)/denominator:sum/normalizer;
            }
        }
        if(best<=0||bestScore<.80){since=-1;return result;}
        if(result.ready){
            double previous=60000/result.bpm;
            while(best<previous*.70)best*=2;
            while(best>previous*1.40)best/=2;
            if(std::abs(best-previous)>previous*.12){since=-1;return result;}
            double robust=refine(best,now);if(robust<=0)return result;best=robust;
        }
        if(since<0||candidate<=0||std::abs(best-candidate)>candidate*.025){candidate=best;since=now;}
        else candidate=candidate*.65+best*.35;
        if(now-since>=400){
            if(!result.ready)result={true,60000/candidate,float(bestScore),true};
            else{
                double previous=60000/result.bpm;
                double next=previous+std::clamp((candidate-previous)*.45,-previous*.004,previous*.004);
                if(std::abs(next-previous)>.002){result.bpm=60000/next;result.confidence=float(bestScore);result.updated=true;}
            }
        }return result;
    }
};
}
