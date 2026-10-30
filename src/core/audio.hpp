#pragma once
#include "classifier.hpp"
#include "engine.hpp"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <cstring>

namespace kp {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr) {
    if(FAILED(hr)) { char message[80]; sprintf_s(message,"Audio de Windows: 0x%08lX",static_cast<unsigned long>(hr)); throw std::runtime_error(message); }
}
class ProcessActivation final: public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    Microsoft::WRL::FtmBase,IActivateAudioInterfaceCompletionHandler> {
public:
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HRESULT result=E_PENDING;
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    PROPVARIANT blob{};
    ComPtr<IAudioClient> client;
    ~ProcessActivation(){CloseHandle(done);}
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT activated=E_FAIL;ComPtr<IUnknown> unknown;
        result=operation->GetActivateResult(&activated,&unknown);
        if(SUCCEEDED(result))result=activated;
        if(SUCCEEDED(result))result=unknown.As(&client);
        SetEvent(done);return S_OK;
    }
};
inline ComPtr<IAudioClient> processAudio(HANDLE stopEvent){
    auto completion=Microsoft::WRL::Make<ProcessActivation>();
    auto& params=completion->params;
    params.ActivationType=AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId=GetCurrentProcessId();
    params.ProcessLoopbackParams.ProcessLoopbackMode=PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    auto& blob=completion->blob;blob.vt=VT_BLOB;blob.blob.cbSize=sizeof(params);blob.blob.pBlobData=reinterpret_cast<BYTE*>(&params);
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    check(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,__uuidof(IAudioClient),&blob,completion.Get(),&operation));
    HANDLE waits[]={stopEvent,completion->done};
    if(WaitForMultipleObjects(2,waits,FALSE,5000)!=WAIT_OBJECT_0+1)throw std::runtime_error("Captura de aplicaciones no disponible");
    check(completion->result);return completion->client;
}
class DeviceNotification final: public IMMNotificationClient {
    std::atomic<ULONG> refs{1}; HANDLE changed; bool follow;
public:
    DeviceNotification(HANDLE event,bool automatic):changed(event),follow(automatic){}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out) return E_POINTER;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IMMNotificationClient)) { *out=static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK; }
        *out=nullptr; return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole role,LPCWSTR) override {
        if(follow&&flow==eRender&&role==eConsole) SetEvent(changed); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD) override { SetEvent(changed); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { SetEvent(changed); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { SetEvent(changed); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY) override { return S_OK; }
};

class AudioEngine {
    Shared& state; MusicClassifier& classifier; BeatClock* clock=nullptr;
    HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE changedEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    std::thread worker;
    std::wstring selected;
    void capture(IMMDeviceEnumerator* enumerator) {
        ComPtr<IAudioClient> client;
        bool process=selected==L"@system";
        if(process){try{client=processAudio(stopEvent);}catch(...){process=false;}}
        WAVEFORMATEX systemFormat{};
        std::unique_ptr<WAVEFORMATEX,decltype(&CoTaskMemFree)> format(nullptr,CoTaskMemFree);
        WAVEFORMATEX* raw=nullptr;
        if(process){
            systemFormat.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;systemFormat.nChannels=2;systemFormat.nSamplesPerSec=48000;
            systemFormat.wBitsPerSample=32;systemFormat.nBlockAlign=8;systemFormat.nAvgBytesPerSec=384000;raw=&systemFormat;
        }else{
            ComPtr<IMMDevice> device;
            if(selected.empty()||selected==L"@system")check(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device));
            else check(enumerator->GetDevice(selected.c_str(),&device));
            check(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())));
            check(client->GetMixFormat(&raw));format.reset(raw);
        }
        {std::lock_guard<std::mutex> lock(state.textMutex);state.error.clear();}
        bool floating=raw->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
        bool pcm=raw->wFormatTag==WAVE_FORMAT_PCM;
        if(raw->wFormatTag==WAVE_FORMAT_EXTENSIBLE && raw->cbSize>=22) {
            auto ext=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(raw);
            floating=ext->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            pcm=ext->SubFormat==KSDATAFORMAT_SUBTYPE_PCM;
        }
        int bits=raw->wBitsPerSample,channels=raw->nChannels;
        if((!floating&&!pcm) || (floating&&bits!=32) || (pcm&&bits!=16&&bits!=24&&bits!=32) || channels<1)
            throw std::runtime_error("Formato de audio no compatible (se requiere PCM 16/24/32 o float32)");
        // Zero requested buffer: use the existing engine period without changing system playback.
        DWORD streamFlags=AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        if(process)streamFlags|=AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,streamFlags,0,0,raw,nullptr));
        HANDLE audioEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!audioEvent) throw std::runtime_error("No se pudo crear el evento de audio");
        struct EventCloser { HANDLE h; ~EventCloser(){CloseHandle(h);} } closer{audioEvent};
        check(client->SetEventHandle(audioEvent));
        ComPtr<IAudioCaptureClient> capture; check(client->GetService(IID_PPV_ARGS(&capture)));
        Resampler16k resampler; resampler.reset(raw->nSamplesPerSec);
        state.rate=raw->nSamplesPerSec;state.captureOrigin=0;if(clock)clock->beginSource();classifier.reset();
        uint64_t sourceFrames=0;
        check(client->Start()); state.running=true;state.notify();
        struct StopClient { IAudioClient* p; ~StopClient(){p->Stop();} } stopper{client.Get()};
        HANDLE waits[]={stopEvent,changedEvent,audioEvent};
        for(;;) {
            DWORD wait=WaitForMultipleObjects(3,waits,FALSE,250);
            if(wait==WAIT_OBJECT_0||wait==WAIT_OBJECT_0+1) break;
            if(wait==WAIT_FAILED) throw std::runtime_error("Fallo al esperar el audio");
            UINT packet=0; check(capture->GetNextPacketSize(&packet));
            while(packet) {
                BYTE* data=nullptr; UINT frames=0; DWORD flags=0; UINT64 devicePosition=0,qpc=0;
                check(capture->GetBuffer(&data,&frames,&flags,&devicePosition,&qpc));
                if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                    ++state.discontinuities;sourceFrames=0;state.captureOrigin=0;if(clock)clock->beginSource();classifier.reset();
                }
                double sampleStart=(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)||!qpc ?
                    nowMs()-frames*1000.0/raw->nSamplesPerSec : qpc*.0001;
                if(state.captureOrigin.load()&&std::abs(sampleStart-(state.captureOrigin.load()+sourceFrames*1000.0/raw->nSamplesPerSec))>50){
                    ++state.discontinuities;sourceFrames=0;if(clock)clock->beginSource();classifier.reset();state.captureOrigin=sampleStart;
                }
                if(!state.captureOrigin.load())state.captureOrigin=sampleStart;
                bool silent=(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0;
                state.lastPacket=nowMs();
                double packetPower=0;
                for(UINT i=0;i<frames;++i) {
                    float mono=0;
                    if(!silent) for(int channel=0;channel<channels;++channel) {
                        const BYTE* p=data+size_t(i)*raw->nBlockAlign+channel*(bits/8);
                        float sample=0;
                        if(floating) std::memcpy(&sample,p,4);
                        else if(bits==16) { int16_t v; std::memcpy(&v,p,2); sample=v/32768.f; }
                        else if(bits==24) { int32_t v=int32_t(p[0])|(int32_t(p[1])<<8)|(int32_t(p[2])<<16); if(v&0x800000) v|=int32_t(0xff000000); sample=v/8388608.f; }
                        else { int32_t v; std::memcpy(&v,p,4); sample=float(v/2147483648.0); }
                        if(std::isfinite(sample)) mono+=sample/channels;
                    }
                    packetPower+=double(mono)*mono;
                    if(clock)clock->push(mono);
                    resampler.step(mono,[this](float value){classifier.push(value);});
                }
                if(frames&&packetPower/frames>1e-8)state.lastAudible=nowMs();
                sourceFrames+=frames;
                check(capture->ReleaseBuffer(frames));
                check(capture->GetNextPacketSize(&packet));
            }
        }
        state.running=false;
    }
    void run() {
        HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        DWORD task=0; HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Audio",&task);
        try {
            ComPtr<IMMDeviceEnumerator> enumerator;
            check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)));
            auto* notification=new DeviceNotification(changedEvent,selected.empty());
            HRESULT registered=enumerator->RegisterEndpointNotificationCallback(notification);
            for(;;) {
                if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0) break;
                try { capture(enumerator.Get()); }
                catch(const std::exception& e) {
                    state.running=false; classifier.reset();
                    std::lock_guard<std::mutex> lock(state.textMutex); state.error=wide(e.what());state.notify();
                }
                if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0) break;
                state.running=false; classifier.reset();
                HANDLE waits[]={stopEvent,changedEvent};
                if(WaitForMultipleObjects(2,waits,FALSE,1000)==WAIT_OBJECT_0) break;
            }
            if(SUCCEEDED(registered)) enumerator->UnregisterEndpointNotificationCallback(notification);
            notification->Release();
        } catch(const std::exception& e) {
            std::lock_guard<std::mutex> lock(state.textMutex); state.error=wide(e.what());state.notify();
        }
        state.running=false;
        if(mmcss) AvRevertMmThreadCharacteristics(mmcss);
        if(SUCCEEDED(initialized)) CoUninitialize();
    }
public:
    AudioEngine(Shared& s,MusicClassifier& c,BeatClock* d=nullptr):state(s),classifier(c),clock(d){}
    ~AudioEngine(){stop();CloseHandle(stopEvent);CloseHandle(changedEvent);}
    void start(const std::wstring& id=L"@system") {
        stop(); selected=id; ResetEvent(stopEvent); ResetEvent(changedEvent);
        classifier.reset(); worker=std::thread([this]{run();});
    }
    void stop() {
        SetEvent(stopEvent); if(worker.joinable()) worker.join();
        state.running=false; classifier.reset();
    }
};
}
