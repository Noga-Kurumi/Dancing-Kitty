#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

namespace kp {
inline double nowMs() {
    static const double scale=[](){ LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1000.0/f.QuadPart; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart*scale;
}
inline std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    std::wstring out(n,L' '); MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n); return out;
}
struct Shared {
    std::wstring modelDirectory;
    HANDLE notificationEvent=nullptr; // Owned by the coordinator, outlives workers.
    HANDLE clientNotificationEvent=nullptr;
    struct PublishedPlan {bool valid=false;double origin=0,period=0,expires=0;};
    mutable std::mutex planMutex;PublishedPlan publishedPlan;
    std::atomic<bool> bMusic{false},running{false};
    std::atomic<bool> transportPlaying{false};
    std::atomic<int> modelState{0},beatModelState{0},generation{0};
    std::atomic<int> classifiedGeneration{-1};
    std::atomic<bool> beatEnabled{false};
    std::atomic<int> tempoGeneration{0};
    std::atomic<double> catalogBpm{0},playbackRate{1},effectiveBpm{0},beatInterval{0};
    std::atomic<bool> beatPhaseLocked{false};
    std::atomic<bool> phaseProvisional{false};
    std::atomic<bool> localTempoMode{false},localTempoReady{false};
    std::atomic<double> localTempoBpm{0};
    std::atomic<uint64_t> localTempoRefinements{0};
    std::atomic<float> localTempoConfidence{0};
    std::atomic<int> pulseMode{0},pulseDivision{1};
    std::atomic<double> pulseIntervalMs{0};
    std::atomic<uint64_t> phaseCorrections{0};
    std::atomic<float> phaseAgreement{0};
    std::atomic<int> phaseEvidence{0};
    std::atomic<double> nextBeatQpcMs{0};
    std::atomic<double> lastPacket{0},lastHit{0},sampleAge{0},musicUpdated{0};
    std::atomic<double> captureOrigin{0};
    std::atomic<int> trackingState{0},relearn{0};
    std::atomic<float> trackingConfidence{0},learningProgress{0},beatScore{0};
    std::atomic<double> lastAudible{0},schedulerErrorMs{0},maxSchedulerErrorMs{0};
    std::atomic<uint64_t> skippedPulses{0};
    std::atomic<uint64_t> pulses{0},overflows{0},discontinuities{0};
    std::atomic<uint64_t> catalogRequests{0},classifierWindows{0};
    std::atomic<int> rate{0};
    std::mutex textMutex;
    std::wstring error,modelError,beatError;
    void notifyClient()const{if(clientNotificationEvent)SetEvent(clientNotificationEvent);}
    void notify()const{if(notificationEvent)SetEvent(notificationEvent);notifyClient();}
};
}
