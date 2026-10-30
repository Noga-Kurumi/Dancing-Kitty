#pragma once
#include "common.hpp"
#include "dsp.hpp"
#include "beatnet_bins.hpp"
#include <onnxruntime_cxx_api.h>
#include <complex>
#include <memory>

namespace kp {
// Fixed-size Bluestein transform preserves BeatNet's original odd 1411-sample
// window. Padding that window to a different FFT length changes its frontend.
class BeatFeatures {
    static constexpr size_t N=1411,M=4096;
    using C=std::complex<float>;
    std::array<float,N> audio{},hann{};
    std::array<C,N> chirp{};
    std::array<C,M> kernel{},work{};
    std::array<C,M/2> roots{};
    std::array<size_t,M> reverse{};
    std::array<float,136> previous{};
    size_t position=N/2;uint64_t received=0;
    void transform(std::array<C,M>& values,bool inverse=false){
        for(size_t i=0;i<M;++i)if(i<reverse[i])std::swap(values[i],values[reverse[i]]);
        for(size_t size=2;size<=M;size*=2){size_t half=size/2,stride=M/size;
            for(size_t start=0;start<M;start+=size)for(size_t j=0;j<half;++j){
                auto root=inverse?std::conj(roots[j*stride]):roots[j*stride];
                auto even=values[start+j],odd=values[start+j+half]*root;
                values[start+j]=even+odd;values[start+j+half]=even-odd;
            }
        }
        if(inverse)for(auto& v:values)v/=float(M);
    }
public:
    BeatFeatures(){
        for(size_t i=0;i<M;++i){size_t r=0,v=i;for(int bit=0;bit<12;++bit){r=(r<<1)|(v&1);v>>=1;}reverse[i]=r;
            if(i<M/2)roots[i]=C(float(std::cos(-2*pi*i/M)),float(std::sin(-2*pi*i/M)));}
        for(size_t i=0;i<N;++i){double angle=-pi*double(i)*double(i)/N;chirp[i]=C(float(std::cos(angle)),float(std::sin(angle)));
            hann[i]=float(.5-.5*std::cos(2*pi*i/(N-1)));kernel[i]=std::conj(chirp[i]);if(i)kernel[M-i]=kernel[i];}
        transform(kernel);
    }
    void reset(){audio.fill(0);previous.fill(0);position=N/2;received=0;}
    template<class Sink> void push(float sample,Sink sink){
        audio[position]=sample;position=(position+1)%N;++received;
        if(received<706||(received-706)%441)return;
        work.fill({});double power=0;
        for(size_t i=0;i<N;++i){float v=audio[(position+i)%N];power+=double(v)*v;work[i]=v*hann[i]*chirp[i];}
        transform(work);for(size_t i=0;i<M;++i)work[i]*=kernel[i];transform(work,true);
        std::array<float,272> features{};
        for(size_t band=0;band<136;++band){int l=beatBins[band],c=beatBins[band+1],r=beatBins[band+2];float sum=0,norm=0;
            for(int b=l;b<r;++b){float weight=b<c?float(b-l)/(c-l):float(r-b)/(r-c);sum+=std::abs(work[b]*chirp[b])*weight;norm+=weight;}
            float v=std::log10(1+sum/norm);features[band]=v;
            features[band+136]=received==706?0:std::max(0.f,v-previous[band]);previous[band]=v;
        }
        sink(features,int64_t((received-706)/441),float(std::sqrt(power/N)));
    }
};

class AnalysisResampler {
    double inputRate=48000,outputRate=22050,position=0,next=0;float previous=0;Biquad a,b;bool filter=false;
public:
    void reset(double input,double output=22050){inputRate=input;outputRate=output;position=next=0;previous=0;filter=input>output;
        if(filter){a.setup(inputRate,outputRate*.43,.541196);b.setup(inputRate,outputRate*.43,1.306563);}}
    template<class Sink> void push(float input,Sink sink){
        if(inputRate==outputRate){sink(input);return;}
        float value=filter?b.step(a.step(input)):input;
        while(next<=position){double blend=std::clamp(next-(position-1),0.0,1.0);sink(float(previous+(value-previous)*blend));next+=inputRate/outputRate;}
        previous=value;++position;
    }
};

class BeatNetwork {
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR,"KickPulseBeat"};Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
    std::array<float,300> hidden{},cell{};
public:
    explicit BeatNetwork(const std::wstring& path){
        options.SetIntraOpNumThreads(1);options.SetInterOpNumThreads(1);options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        options.AddConfigEntry("session.intra_op.allow_spinning","0");options.AddConfigEntry("session.inter_op.allow_spinning","0");
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);session=std::make_unique<Ort::Session>(env,path.c_str(),options);
    }
    void reset(){hidden.fill(0);cell.fill(0);}
    std::array<float,3> process(std::array<float,272>& features){
        const std::array<int64_t,3> xShape{1,1,272},stateShape{2,1,150};
        std::array<Ort::Value,3> input{
            Ort::Value::CreateTensor<float>(memory,features.data(),272,xShape.data(),3),
            Ort::Value::CreateTensor<float>(memory,hidden.data(),300,stateShape.data(),3),
            Ort::Value::CreateTensor<float>(memory,cell.data(),300,stateShape.data(),3)};
        const char* names[]={"features","hidden","cell"};const char* outputs[]={"probabilities","next_hidden","next_cell"};
        auto result=session->Run(Ort::RunOptions{nullptr},names,input.data(),3,outputs,3);
        std::array<float,3> probabilities{};std::copy_n(result[0].GetTensorData<float>(),3,probabilities.data());
        std::copy_n(result[1].GetTensorData<float>(),300,hidden.data());std::copy_n(result[2].GetTensorData<float>(),300,cell.data());return probabilities;
    }
};

// Local maxima are observations for a clock, never animation triggers.
class BeatPeaks {
    std::array<float,5> history{};double last=-1e9,started=-1;
public:
    void reset(){history.fill(0);last=-1e9;started=-1;}
    template<class Sink> void push(float probability,double time,Sink sink){
        std::move(history.begin()+1,history.end(),history.begin());history.back()=probability;
        if(started<0)started=time;
        float p=history[2];double stamp=time-40;
        if(stamp-started>=160&&p>=.15f&&p>=history[0]&&p>=history[1]&&p>history[3]&&p>history[4]&&stamp-last>=160){last=stamp;sink(stamp,p);}
    }
};
}
