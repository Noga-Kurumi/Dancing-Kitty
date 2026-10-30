#pragma once
#include <string>
#include <cstdint>

namespace kp {
enum class MusicLookupState {WaitingForPlayer,CheckingMusic,NonMusic,ReadingMetadata,LookingUpBpm,BpmReady,BpmUnavailable,AmbiguousPlayback,Error,EstimatingBpm};
struct TrackMetadata {
    std::wstring title,artist,album;
    double durationSeconds=0;
};
struct BpmResult {
    bool found=false,fromCache=false,estimated=false;
    double bpm=0,matchConfidence=0;
    std::wstring source,sourceUrl,matchedTitle,matchedArtist,reason;
};
}
