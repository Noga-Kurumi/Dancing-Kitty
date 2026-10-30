#pragma once
#include "tracking_state.hpp"
#include "media_types.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace kp {
struct BeatStatus {
    bool monitoring=false,playerPlaying=false;
    MusicLookupState lookupState=MusicLookupState::WaitingForPlayer;
    std::wstring playerAppId;
    TrackMetadata track;
    BpmResult catalog;
    bool beatClockEnabled=false,beatPhaseLocked=false;
    bool phaseProvisional=false;
    PulseMode pulseMode=PulseMode::Auto;
    int pulseDivision=1;
    double pulseBpm=0,pulseIntervalMs=0;
    double effectiveBpm=0,playbackRate=1,beatIntervalMs=0,nextBeatQpcMs=0;
    bool pulseScheduleValid=false;
    double nextPulseQpcMs=0; // Output pulse, including Auto/HalfTime division.
    int tempoRevision=0;
    uint64_t phaseCorrections=0;
    uint64_t localTempoRefinements=0;
    float localTempoConfidence=0;
    float phaseAgreement=0;
    int phaseEvidence=0;
    int captureGeneration=0,beatModelState=0;
    uint64_t catalogRequests=0,classifierWindows=0,overflows=0,discontinuities=0;
    double audioAgeMs=0,schedulerErrorMs=0,maxSchedulerErrorMs=0;
    bool capturing=false;
    bool bMusic=false;
    TrackingState state=TrackingState::Waiting;
    float confidence=0;
    float learningProgress=0;
    uint64_t beatCount=0; // Regular musical pulses after alignment and pulse division.
    // QueryPerformanceCounter time expressed in milliseconds, not wall time.
    double lastBeatQpcMs=0;
    std::wstring error;
};

// start() subscribes to Windows media events. Audio classification is enabled
// only while exactly one media session is Playing; catalogue lookup requires
// a fresh positive music classification. There is no UI or account setup.
class BeatEngine final {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit BeatEngine(std::wstring modelDirectory,bool offline=false);
    ~BeatEngine();
    BeatEngine(const BeatEngine&)=delete;
    BeatEngine& operator=(const BeatEngine&)=delete;

    void start();
    void stop();
    void relearn();
    void setPulseMode(PulseMode mode);
    BeatStatus status()const;
    // Windows auto-reset event, owned by this engine. One external consumer
    // may wait on it for pulse/status/phase changes; never close or signal it.
    void* nativeEventHandle()const;
};
}
