#pragma once
#include "types.hpp"
#include <array>
#include <cstdint>

namespace neh {
enum class ReplacementReason { None,LargeRoster,DuplicateCharacter,NativeMenuOff };
inline const char* ReplacementReasonText(ReplacementReason reason) {
    switch(reason) {
    case ReplacementReason::LargeRoster:return "nine_or_more_survivors";
    case ReplacementReason::DuplicateCharacter:return "duplicate_survivor_character";
    case ReplacementReason::NativeMenuOff:return "native_menu_off";
    default:return "none";
    }
}
struct ReplacementObservation {
    ReplacementReason reason=ReplacementReason::None;
    bool healthyUnique=false;
};
inline ReplacementObservation ObserveReplacement(const Snapshot& snapshot) {
    ReplacementObservation out;
    if(!snapshot.inGame)return out;
    std::array<bool,kMaxPlayers+1> players{};
    std::array<bool,8> characters{};
    unsigned count=0;bool duplicate=false,healthy=true;
    for(const auto& player:snapshot.players) {
        if(player.index<1||player.index>kMaxPlayers||players[player.index])continue;
        players[player.index]=true;++count;
        if(!player.alive||player.incapacitated||player.hanging)healthy=false;
        if(player.character<0||player.character>=8) {healthy=false;continue;}
        if(characters[player.character])duplicate=true;
        characters[player.character]=true;
    }
    // Character IDs name Nick/Rochelle/Coach/Ellis/Bill/Zoey/Francis/Louis.
    // Player nicknames and Neko model/portrait IDs never select this mode.
    out.reason=count>=9?ReplacementReason::LargeRoster:duplicate?ReplacementReason::DuplicateCharacter:ReplacementReason::None;
    out.healthyUnique=count>0 && healthy && !duplicate && count<9;
    return out;
}
class ReplacementPolicy {
public:
    void Reset() {*this={};}
    ReplacementReason Observe(const Snapshot& snapshot,std::uint32_t rootHandle,std::uint32_t now) {
        if(rootHandle==0xffffffffu)return reason_;
        if(!hasRoot_||rootHandle_!=rootHandle) {Reset();hasRoot_=true;rootHandle_=rootHandle;}
        const auto observation=ObserveReplacement(snapshot);
        if(observation.reason!=ReplacementReason::None) {
            reason_=observation.reason;releasePending_=false;return reason_;
        }
        if(reason_==ReplacementReason::None)return reason_;
        // Missing character reads, incapacitation and death cannot make the
        // original buggy branch alternate on/off. Restore only after two
        // continuous seconds of a fully known healthy, unique roster.
        if(!observation.healthyUnique) {releasePending_=false;return reason_;}
        if(!releasePending_) {releasePending_=true;releaseSince_=now;}
        if(static_cast<std::uint32_t>(now-releaseSince_)>=2000) {reason_=ReplacementReason::None;releasePending_=false;}
        return reason_;
    }
private:
    ReplacementReason reason_=ReplacementReason::None;
    std::uint32_t rootHandle_=0xffffffffu,releaseSince_=0;
    bool hasRoot_=false,releasePending_=false;
};
} // namespace neh
