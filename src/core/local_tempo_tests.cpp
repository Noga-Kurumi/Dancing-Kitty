#include "local_tempo.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    try{
        if(argc>1){kp::LocalTempoEstimator estimator;std::ifstream input(argv[1]);std::string line;
            while(std::getline(input,line)){std::stringstream row(line);std::string type,time,weight;std::getline(row,type,',');std::getline(row,time,',');std::getline(row,weight,',');
                if(type!="beat")continue;double stamp=std::stod(time)*1000;estimator.observe(stamp,float(std::stod(weight)));auto value=estimator.advance(stamp);
                if(value.ready){std::cout<<"LOCAL_BPM="<<value.bpm<<" confidence="<<value.confidence<<" ready_at_seconds="<<stamp/1000<<'\n';return 0;}}
            std::cerr<<"NO_RELIABLE_LOCAL_TEMPO\n";return 1;
        }
        for(double bpm:{79.8,127.2,120.}){kp::LocalTempoEstimator estimator;kp::LocalTempoResult value;double period=60000/bpm;
            for(int i=0;i<24;++i){double stamp=123+i*period+(i%3-1)*5;estimator.observe(stamp,.95f);value=estimator.advance(stamp);}
            check(value.ready&&std::abs(value.bpm-bpm)<bpm*.015,"Local tempo is inaccurate on stable beats");}
        kp::LocalTempoEstimator weak;for(int i=0;i<40;++i){weak.observe(i*378.,.3f);check(!weak.advance(i*378.).ready,"Weak intro invents a local BPM");}
        kp::LocalTempoEstimator irregular;double stamp=0;for(double gap:{311.,857.,421.,1103.,274.,673.,919.,353.,1237.}){stamp+=gap;irregular.observe(stamp,.9f);check(!irregular.advance(stamp).ready,"Irregular attacks invent a local BPM");}
        kp::LocalTempoEstimator adapting;kp::LocalTempoResult tracked;double first=0;stamp=0;int revisions=0;
        for(int i=0;i<130;++i){double period=i<22?490.:500.;stamp+=period;adapting.observe(stamp+(i%3-1)*4,.95f);auto next=adapting.advance(stamp+10);
            if(next.ready&&!first)first=next.bpm;
            if(next.updated&&tracked.ready){check(std::abs(next.bpm-tracked.bpm)<tracked.bpm*.0041,"Refinement jumps the tempo");++revisions;}
            tracked=next;
        }
        check(first>121.5&&tracked.ready&&std::abs(tracked.bpm-120)<.12&&revisions>5,"Rolling tempo freezes its initial error");
        auto before=tracked.bpm;
        for(int i=0;i<100;++i){stamp+=20;auto value=adapting.advance(stamp);check(!value.updated&&value.bpm==before,"Old evidence repeatedly refines the BPM");}
        // Missing every other hit must keep the established octave. Stale or
        // invalid input must not drag the accepted tempo towards random audio.
        for(int i=0;i<40;++i){stamp+=1000;adapting.observe(stamp,.95f);tracked=adapting.advance(stamp);}
        check(std::abs(tracked.bpm-120)<.3,"Missing hits halved the established tempo");
        adapting.observe(NAN,.9f);adapting.observe(stamp+500,NAN);check(std::isfinite(adapting.advance(stamp+500).bpm),"Invalid observation poisoned the tempo");
        std::cout<<"PASS local tempo: stable slow/fast beats, jitter, weak intro and irregular attacks\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
