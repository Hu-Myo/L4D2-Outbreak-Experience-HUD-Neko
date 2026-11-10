#include "types.hpp"
#include "hud_policy.hpp"
#include "survivor_character.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>

using namespace neh;
namespace {
int checks=0;
void Check(bool value,const char* message) {
    if(!value){std::cerr<<"FAILED: "<<message<<'\n';std::exit(1);}++checks;
}
Snapshot Roster(int teammates) {
    Snapshot s;s.inGame=true;s.localIndex=s.viewIndex=1;
    for(int i=1;i<=teammates+1;++i) {Player p;p.index=i;p.name="Player "+std::to_string(i);p.alive=true;p.health=100;s.players.push_back(p);}
    return s;
}
Settings Config() {Settings c;c.scale=1;c.gap=c.rowGap=0;return c;}
std::vector<HudSlot> Native(int count=4,int width=100,int height=40,int screenHeight=480,int gap=0) {
    std::vector<HudSlot> result;
    for(int n=0;n<count;++n)result.push_back({static_cast<unsigned>(100+n),n+2,{n*(width+gap),screenHeight-height,width,height},true});
    return result;
}
std::vector<HudSlot> UpperNative(int count=3) {
    auto result=Native(count);
    for(int n=0;n<count;++n)result[n].bounds={500+n*110,20+n*60,100,40};
    return result;
}
std::vector<int> Ids(const Layout& layout) {
    std::vector<int> result;for(const auto& card:layout.cards)result.push_back(card.playerIndex);return result;
}
bool Overlap(const Rect& a,const Rect& b) {
    return a.w>0&&a.h>0&&b.w>0&&b.h>0&&static_cast<long long>(a.x)<static_cast<long long>(b.x)+b.w&&
        static_cast<long long>(a.x)+a.w>b.x&&static_cast<long long>(a.y)<static_cast<long long>(b.y)+b.h&&
        static_cast<long long>(a.y)+a.h>b.y;
}
Rect Visible(const HudSlot& slot,int width,int height) {
    long long l=std::max(0,slot.bounds.x),t=std::max(0,slot.bounds.y);
    long long r=std::min<long long>(width,static_cast<long long>(slot.bounds.x)+slot.bounds.w);
    long long b=std::min<long long>(height,static_cast<long long>(slot.bounds.y)+slot.bounds.h);
    if(slot.hasVisibleBounds) {
        l=std::max<long long>(l,slot.visibleBounds.x);t=std::max<long long>(t,slot.visibleBounds.y);
        r=std::min(r,static_cast<long long>(slot.visibleBounds.x)+slot.visibleBounds.w);
        b=std::min(b,static_cast<long long>(slot.visibleBounds.y)+slot.visibleBounds.h);
    }
    return {static_cast<int>(l),static_cast<int>(t),static_cast<int>(std::max(0LL,r-l)),static_cast<int>(std::max(0LL,b-t))};
}
bool Safe(const Layout& layout,const Snapshot& snapshot,const std::vector<HudSlot>& native,int width,int height,bool mirrored=false) {
    std::set<int> ids;
    for(std::size_t n=0;n<layout.cards.size();++n) {
        const auto& card=layout.cards[n];const auto& r=card.bounds;
        if(card.playerIndex==snapshot.localIndex||card.playerIndex==snapshot.viewIndex||!ids.insert(card.playerIndex).second||
           r.w<=0||r.h<=0||r.x<0||r.y<0||static_cast<long long>(r.x)+r.w>width||static_cast<long long>(r.y)+r.h>height)return false;
        for(const auto& slot:native)if(slot.visible) {
            if((!mirrored&&slot.playerIndex==card.playerIndex)||Overlap(r,Visible(slot,width,height)))return false;
        }
        if(snapshot.hasSelfHudBounds&&Overlap(r,snapshot.selfHudBounds))return false;
        for(std::size_t previous=0;previous<n;++previous)if(Overlap(r,layout.cards[previous].bounds))return false;
    }
    return true;
}
void CheckIdentitiesAndCapacity() {
    auto cfg=Config();const auto roster=Roster(7);auto three=Native(3),four=Native();
    auto a=PlanLayout(roster,three,cfg,960,480),b=PlanLayout(roster,four,cfg,960,480);
    Check(!a.suppressed&&a.nativeCount==3&&Ids(a)==std::vector<int>{5,6,7,8},"three real native cards supplement all four missing teammates in an eight-player game");
    Check(!b.suppressed&&b.nativeCount==4&&Ids(b)==std::vector<int>{6,7,8},"four real native cards keep the usual three additions");
    Check(a.cards.front().ordinal==4&&a.cards.back().ordinal==7&&b.cards.front().ordinal==5,"supplemental teammate ordinal four remains visible with overflow off");
    Check(a.cards.front().bounds.x==300&&a.cards.front().bounds.y==440&&b.cards.front().bounds.x==400,"contiguous lower native cards naturally lead into the next free position");
    Check(Ids(PlanLayout(Roster(4),three,cfg,960,480))==std::vector<int>{5},"a fourth teammate missing from three native cards is supplemented even in a five-survivor game");
    Check(PlanLayout(Roster(3),three,cfg,960,480).cards.empty()&&PlanLayout(Roster(4),four,cfg,960,480).cards.empty(),"fully represented small rosters need no normal additions");
    four.back().visible=false;
    Check(Ids(PlanLayout(roster,four,cfg,960,480))==std::vector<int>{5,6,7,8},"a hidden fourth native card is supplemented rather than suppressing three valid visible cards");
    four.back().visible=true;const auto appeared=PlanLayout(roster,four,cfg,960,480);
    Check(Ids(appeared)==std::vector<int>{6,7,8}&&appeared.cards.front().bounds.x==400,"a fourth native card appearing removes only its actual supplemental identity");
    three[0].playerIndex=8;three[1].playerIndex=2;three[2].playerIndex=6;
    Check(Ids(PlanLayout(roster,three,cfg,960,480))==std::vector<int>{3,4,5,7},"missing identities come from actual native targets rather than roster position or character");
    const auto capped3=PlanLayout(Roster(8),Native(3),cfg,960,480),capped4=PlanLayout(Roster(8),Native(),cfg,960,480);
    Check(Ids(capped3)==std::vector<int>{5,6,7,8}&&capped3.hiddenOverflow==1&&capped4.cards.size()==3&&capped4.hiddenOverflow==1,"overflow off limits total visible teammates to seven for either native count");
    cfg.overflow=true;
    const auto all3=PlanLayout(Roster(31),Native(3),cfg,960,480),all4=PlanLayout(Roster(31),Native(),cfg,960,480);
    Check(all3.cards.size()==28&&all4.cards.size()==27&&all3.hiddenOverflow==0&&all4.hiddenOverflow==0&&
          all3.cards.back().ordinal==31&&all4.cards.back().playerIndex==32,"overflow on includes every missing teammate through the thirty-second player slot");
    Check(Safe(all3,Roster(31),Native(3),960,480)&&Safe(all4,Roster(31),Native(),960,480),"large rosters wrap automatically without overlap or duplicate identities");
}
void CheckFirstFitGeometry() {
    auto cfg=Config();const auto roster=Roster(7);auto native=UpperNative();
    auto result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.size()==4&&result.cards.front().bounds.x==0&&result.cards.front().bounds.y==440,"upper and right-side original HUDs do not move the lower-left origin");
    native[0].bounds={30,20,100,40};native[1].bounds={800,200,100,40};native[2].bounds={420,330,100,40};
    result=PlanLayout(roster,native,cfg,960,480);
    Check(!result.suppressed&&result.cards.size()==4&&Safe(result,roster,native,960,480),"vertically scattered native cards are supported without a horizontal-row requirement");
    native=Native(3);native[1].bounds.x=250;native[2].bounds.x=500;
    result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.size()==4&&result.cards[0].bounds.x==100&&result.cards[1].bounds.x==350&&result.cards[2].bounds.x==600,"first-fit fills genuine native gaps before moving beyond later original cards");
    cfg.overflow=true;auto wideRoster=Roster(9);native=Native(3);
    wideRoster.hasSelfHudBounds=true;
    wideRoster.selfHudBounds={0,0,160,80};
    result=PlanLayout(wideRoster,native,cfg,560,480);
    Check(Ids(result)==std::vector<int>{5,6,7,8,9,10}&&result.cards[0].bounds.x==300&&result.cards[1].bounds.x==400&&
          result.cards[2].bounds.x==0&&result.cards[2].bounds.y==400&&result.cards[2].row==1,"available width determines wrapping instead of fixed three- or seven-card columns");
    auto self=wideRoster;self.hasSelfHudBounds=true;self.selfHudBounds={0,400,200,40};
    result=PlanLayout(self,native,cfg,560,480);
    Check(Ids(result)==std::vector<int>{5,6,7,8,9,10}&&result.cards[2].bounds.x==200&&result.cards[2].bounds.y==400&&
          Safe(result,self,native,560,480),"blocked cells advance placement while preserving the current missing player and roster order");
    self=roster;self.hasSelfHudBounds=true;self.selfHudBounds={0,400,960,80};native=UpperNative();
    result=PlanLayout(self,native,cfg,960,480);
    Check(result.cards.size()==4&&result.cards.front().bounds.x==0&&result.cards.front().bounds.y==360&&
          Safe(result,self,native,960,480),"a self HUD covering the bottom rows moves additions upward to the first free row");
    auto blocked=roster;blocked.hasSelfHudBounds=true;blocked.selfHudBounds={0,0,960,480};
    result=PlanLayout(blocked,native,cfg,960,480);
    Check(result.cards.empty()&&result.hiddenOverflow==4&&result.reason=="insufficient_screen_space","complete occupation hides missing players without inventing an overlapping slot");
    native=UpperNative();cfg.offsetX=17;cfg.offsetY=-20;
    result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.front().bounds.x==17&&result.cards.front().bounds.y==420,"configured offsets shift the absolute lower-left start inside the screen");
    cfg.offsetX=-100;cfg.offsetY=100;
    result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.front().bounds.x==0&&result.cards.front().bounds.y==440,"negative left and below-screen bottom offsets clamp safely");
    cfg.offsetX=950;cfg.offsetY=0;
    result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.empty()&&result.hiddenOverflow==4,"insufficient remaining width hides cards instead of silently shrinking their native-derived size");
}
void CheckSelfHudFallback() {
    constexpr int width=2560,height=1440;
    const auto native=Native(3,270,120,height);
    auto roster=Roster(11);
    const auto cfg=Config();
    const Rect unmeasuredSelfHud{2068,1140,492,300};
    const std::vector<int> teammates{2,3,4,5,6,7,8,9,10,11,12};
    auto protectsSelfHud=[&](const Layout& layout) {
        return layout.replacesNative&&layout.hiddenOverflow==0&&Ids(layout)==teammates&&
            layout.cards[6].bounds.x==1620&&layout.cards[6].bounds.y==1320&&
            layout.cards[7].bounds.x==0&&layout.cards[7].bounds.y==1200&&
            Safe(layout,roster,{},width,height)&&
            std::none_of(layout.cards.begin(),layout.cards.end(),[&](const Placement& card) {
                return Overlap(card.bounds,unmeasuredSelfHud);
            });
    };
    Check(protectsSelfHud(PlanLayout(roster,native,cfg,width,height,true)),
          "twelve survivors with undetected self bounds wrap the eighth teammate above the bottom row and preserve the lower-right self HUD");
    roster.hasSelfHudBounds=true;
    for(const Rect invalid:{Rect{2560,1200,480,240},Rect{2080,1200,0,240}}) {
        roster.selfHudBounds=invalid;
        Check(protectsSelfHud(PlanLayout(roster,native,cfg,width,height,true)),
              "fully offscreen or empty self bounds still protect the lower-right self HUD");
    }
    roster.selfHudBounds={0,1320,270,120};
    const auto measured=PlanLayout(roster,native,cfg,width,height,true);
    Check(measured.replacesNative&&Ids(measured)==teammates&&measured.cards.front().bounds.x==270&&
          Safe(measured,roster,{},width,height)&&
          std::any_of(measured.cards.begin(),measured.cards.end(),[&](const Placement& card) {
              return Overlap(card.bounds,unmeasuredSelfHud);
          }),"visible measured self bounds reserve their actual position and leave the unneeded fallback area available");
}
void CheckSizingAndGuards() {
    auto cfg=Config();const auto roster=Roster(7);auto native=UpperNative();
    native[0].bounds.w=80;native[1].bounds.w=100;native[2].bounds.w=120;
    native[0].bounds.h=32;native[1].bounds.h=40;native[2].bounds.h=48;
    cfg.scale=.5f;auto result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.front().bounds.w==50&&result.cards.front().bounds.h==28,"card size uses visible native medians with the minimum readable height");
    cfg.scale=9;result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.front().bounds.w==120&&result.cards.front().bounds.h==48,"excessive scale clamps to the existing supported maximum");
    cfg.scale=std::numeric_limits<float>::quiet_NaN();result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.front().bounds.w==85&&result.cards.front().bounds.h==34,"nonfinite scale uses the existing safe default");
    cfg.scale=1;native=Native(3);
    for(auto& slot:native){slot.bounds.y=430;slot.bounds.h=100;}
    result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.size()==4&&result.cards.front().bounds.h==50&&result.cards.front().bounds.y==430&&
          Safe(result,roster,native,960,480),"offscreen containers contribute only their current visible dimensions and collision area");
    native=Native(3);
    for(auto& slot:native){slot.hasVisibleBounds=true;slot.visibleBounds={slot.bounds.x+10,445,80,30};}
    result=PlanLayout(roster,native,cfg,960,480);
    Check(result.cards.size()==4&&result.cards.front().bounds.w==80&&result.cards.front().bounds.h==30&&
          Safe(result,roster,native,960,480),"parent-clipped native bounds determine card size without blocking invisible container pixels");
    for(const int count:{0,1,2,5}) {
        result=PlanLayout(roster,Native(count),cfg,960,480);
        Check(result.suppressed&&result.cards.empty(),"only three or four real visible native targets are accepted");
    }
    auto bad=Native();bad.back().playerIndex=bad.front().playerIndex;
    Check(PlanLayout(roster,bad,cfg,960,480).suppressed,"duplicate native identities still fail closed even when three unique targets remain");
    bad=Native(3);bad.front().playerIndex=kMaxPlayers+1;
    Check(PlanLayout(roster,bad,cfg,960,480).suppressed,"native targets outside engine slot bounds still fail closed");
    bad=Native(3);bad.front().playerIndex=1;
    Check(PlanLayout(roster,bad,cfg,960,480).suppressed,"local player native panels cannot become teammate anchors");
    for(int invalid=0;invalid<4;++invalid) {
        bad=Native(3);
        if(invalid==0)bad[0].bounds.w=0;
        if(invalid==1)bad[0].bounds.y=490;
        if(invalid==2){bad[0].bounds.y=470;bad[0].bounds.h=40;}
        if(invalid==3){bad[0].hasVisibleBounds=true;bad[0].visibleBounds={0,0,10,10};}
        Check(PlanLayout(roster,bad,cfg,960,480).suppressed,"zero, offscreen, tiny and disjoint clipped native bounds remain invalid");
    }
    for(const auto screen:std::vector<std::pair<int,int>>{{319,480},{960,199},{32769,480},{960,32769}})
        Check(PlanLayout(roster,Native(3),cfg,screen.first,screen.second).suppressed,"invalid or unsupported screen dimensions fail closed");
    cfg.enabled=false;Check(PlanLayout(roster,{},cfg,960,480).cards.empty(),"extension off does not need native data and draws nothing");
    cfg.enabled=true;auto outside=roster;outside.inGame=false;
    Check(PlanLayout(outside,Native(3),cfg,960,480).cards.empty(),"outside a live map no cards are generated");
}
void CheckNativeTakeoverTransition() {
    auto cfg=Config();cfg.debug=true;
    auto roster=Roster(7);
    roster.players.back().alive=false;
    auto partial=roster;
    partial.players.erase(partial.players.begin()+4);
    partial.rosterIncomplete=true;
    auto native=Native();
    native.back().playerIndex=13;

    const auto during=PlanLayout(partial,native,cfg,960,480);
    Check(!during.suppressed&&during.nativeCount==3&&Ids(during)==std::vector<int>{6,7,8}&&
          during.detail=="stale_native_target index=13"&&Safe(during,partial,native,960,480),
          "a stale takeover panel reserves its bounds while three healthy supplemental identities, including a dead teammate, keep their cards");
    const auto rosterFirst=PlanLayout(roster,native,cfg,960,480);
    Check(Ids(rosterFirst)==std::vector<int>{5,6,7,8}&&Safe(rosterFirst,roster,native,960,480),
          "a recovered roster can draw its additional valid identity before the native panel catches up");
    native.back().playerIndex=5;
    Check(Ids(PlanLayout(roster,native,cfg,960,480))==std::vector<int>{6,7,8},
          "when the native target recovers only its own supplemental card disappears");

    native=Native(3);
    native.back().playerIndex=13;
    Check(Ids(PlanLayout(partial,native,cfg,960,480))==std::vector<int>{4,6,7,8},
          "a custom HUD with three panel bounds can supplement valid teammates while one target is stale");
    for (std::size_t i=0;i<native.size();++i)native[i].playerIndex=13+static_cast<int>(i);
    Check(PlanLayout(partial,native,cfg,960,480).suppressed,
          "supplementation still requires a confirmed native teammate");
}
void CheckSinglePlayerAndIdentity() {
    auto cfg=Config();auto roster=Roster(3);const auto native=Native(3);
    Check(!cfg.singlePlayerTest&&PlanLayout(roster,native,cfg,960,480).cards.empty(),"single-player mirroring remains disabled by default");
    cfg.singlePlayerTest=true;
    for(const auto theme:{HudTheme::Current,HudTheme::GameHud}) {
        cfg.theme=theme;const auto result=PlanLayout(roster,native,cfg,960,480);
        Check(Ids(result)==std::vector<int>{2,3,4}&&Safe(result,roster,native,960,480,true),"the explicit test alone duplicates exactly the three existing teammate identities in both themes");
        Check(Ids(PlanLayout(Roster(7),native,cfg,960,480))==std::vector<int>{5,6,7,8},"single-player test on an expanded server keeps ordinary missing-player supplementation");
        Check(Ids(PlanLayout(Roster(4),native,cfg,960,480))==std::vector<int>{5},"four teammates with three native cards supplement only the missing fourth instead of mirroring");
    }
    cfg.singlePlayerTest=false;Check(PlanLayout(roster,native,cfg,960,480).cards.empty(),"turning single-player test off removes only its intentional duplicates");
    cfg.singlePlayerTest=true;roster.players[1].incapacitated=true;roster.players[1].health=245;
    roster.players[2].alive=false;roster.players[2].health=0;roster.players[3].hanging=true;roster.players[3].health=96;
    Check(Ids(PlanLayout(roster,native,cfg,960,480))==std::vector<int>{2,3,4},"downed, dead and hanging teammates keep their actual snapshot identities in test mode");
    Check(PlanLayout(roster,Native(2),cfg,960,480).suppressed,"test mode never fabricates a third native anchor");
    auto observer=Roster(4);observer.viewIndex=2;auto observerNative=Native(3);
    for(auto& slot:observerNative)++slot.playerIndex;
    Check(Ids(PlanLayout(observer,observerNative,cfg,960,480))==std::vector<int>{3,4,5},"both local and spectated player identities are excluded before intentional test mirroring");
    cfg.singlePlayerTest=false;observer=Roster(7);observer.viewIndex=5;
    Check(Ids(PlanLayout(observer,Native(3),cfg,960,480))==std::vector<int>{6,7,8},"normal supplementation excludes the observed survivor as well as the local player");
    observer.players.front().alive=false;
    for (int target : {5,8,2}) {
        observer.viewIndex=target;
        Check(Ids(PlanLayout(observer,Native(3),cfg,960,480))==std::vector<int>{5,6,7,8},
              "after local death, switching the camera neither removes an extra teammate nor duplicates a native one");
        Check(Ids(PlanLayout(observer,Native(3),cfg,960,480,true))==std::vector<int>{2,3,4,5,6,7,8},
              "native replacement retains the complete teammate roster after local death");
    }
    auto duplicateRoster=Roster(7);duplicateRoster.players.push_back(duplicateRoster.players[2]);
    Player invalid;invalid.index=0;duplicateRoster.players.push_back(invalid);invalid.index=33;duplicateRoster.players.push_back(invalid);
    Check(Ids(PlanLayout(duplicateRoster,Native(3),cfg,960,480))==std::vector<int>{5,6,7,8},"duplicate or invalid roster entries cannot create extra copies");
}
void CheckAllRosterSizes() {
    for(const auto theme:{HudTheme::Current,HudTheme::GameHud})for(const auto screen:
        std::vector<std::pair<int,int>>{{320,240},{560,480},{960,480},{1280,720},{1920,1080}})
        for(const int nativeCount:{3,4})for(int teammates=nativeCount;teammates<=31;++teammates) {
            auto cfg=Config();cfg.theme=theme;cfg.overflow=true;cfg.gap=3;cfg.rowGap=4;
            auto roster=Roster(teammates);roster.hasSelfHudBounds=true;
            roster.selfHudBounds={screen.first-100,screen.second-100,100,100};
            const auto native=Native(nativeCount,50,32,screen.second);
            const auto result=PlanLayout(roster,native,cfg,screen.first,screen.second);
            bool order=true;
            for(std::size_t n=0;n<result.cards.size();++n)
                order=order&&result.cards[n].playerIndex==nativeCount+2+static_cast<int>(n)&&
                      result.cards[n].ordinal==nativeCount+1+static_cast<int>(n);
            Check(!result.suppressed&&!result.replacesNative&&order&&result.cards.size()+result.hiddenOverflow==static_cast<std::size_t>(teammates-nativeCount)&&
                  Safe(result,roster,native,screen.first,screen.second),"every supported roster size preserves identity order and safe first-fit placement at varied resolutions");
        }
}
void CheckReplacementLayout() {
    auto cfg=Config();auto roster=Roster(7);
    for(const auto theme:{HudTheme::Current,HudTheme::GameHud})for(int sources=1;sources<=4;++sources) {
        cfg.theme=theme;const auto native=Native(sources);
        const auto result=PlanLayout(roster,native,cfg,960,480,true);
        Check(!result.suppressed&&result.replacesNative&&result.nativeCount==sources&&
              Ids(result)==std::vector<int>{2,3,4,5,6,7,8}&&result.cards.front().ordinal==1&&result.cards.back().ordinal==7,
              "replacement accepts one to four sizing sources and rebuilds each real teammate once in either theme");
        Check(result.cards.front().bounds.x==0&&result.cards.front().bounds.y==440&&
              Overlap(result.cards.front().bounds,native.front().bounds)&&Safe(result,roster,{},960,480),
              "replacement reserves no hidden native area and starts at the actual lower-left corner");
    }
    cfg.theme=HudTheme::Current;auto damaged=Native();
    for(auto& source:damaged)source.playerIndex=32;
    const auto restored=PlanLayout(roster,damaged,cfg,960,480,true);
    Check(restored.replacesNative&&restored.nativeCount==4&&Ids(restored)==std::vector<int>{2,3,4,5,6,7,8}&&
          PlanLayout(roster,damaged,cfg,960,480).suppressed,
          "corrupt duplicated and out-of-roster original targets are sizing-only in replacement mode while normal mode stays fail-closed");
    damaged.front().playerIndex=1;
    Check(Ids(PlanLayout(roster,damaged,cfg,960,480,true))==std::vector<int>{2,3,4,5,6,7,8},
          "replacement never copies a local target from a damaged original card into its actual teammate roster");
    for(const int invalid:{-1,0,33}) {
        auto source=Native(1);source.front().playerIndex=invalid;
        const auto result=PlanLayout(roster,source,cfg,960,480,true);
        Check(result.suppressed&&!result.replacesNative&&result.cards.empty(),"replacement refuses sizing sources whose target is outside engine index bounds");
    }
    for(const int count:{0,5}) {
        const auto result=PlanLayout(roster,Native(count),cfg,960,480,true);
        Check(result.suppressed&&!result.replacesNative&&result.cards.empty(),"missing or unsupported source counts leave the original HUD eligible to paint");
    }
    for(int invalid=0;invalid<4;++invalid) {
        auto source=Native(1);
        if(invalid==0)source.front().bounds.w=0;
        if(invalid==1)source.front().bounds.y=490;
        if(invalid==2){source.front().bounds.y=470;source.front().bounds.h=40;}
        if(invalid==3){source.front().hasVisibleBounds=true;source.front().visibleBounds={0,0,10,10};}
        const auto result=PlanLayout(roster,source,cfg,960,480,true);
        Check(result.suppressed&&!result.replacesNative&&result.cards.empty(),"invalid replacement geometry cannot claim ownership of the original HUD");
    }
    auto source=Native();for(std::size_t n=1;n<source.size();++n)source[n].visible=false;
    Check(PlanLayout(roster,source,cfg,960,480,true).replacesNative,
          "only visible sizing sources count toward replacement's one-to-four source requirement");
    roster.hasSelfHudBounds=true;roster.selfHudBounds={0,440,200,40};
    const auto self=PlanLayout(roster,Native(),cfg,960,480,true);
    Check(self.replacesNative&&self.cards.front().bounds.x==200&&self.cards.front().bounds.y==440&&Safe(self,roster,{},960,480),
          "replacement ignores native occupancy but still reserves the complete self HUD");
    roster.selfHudBounds={0,0,960,480};
    const auto blocked=PlanLayout(roster,Native(1),cfg,960,480,true);
    Check(blocked.cards.empty()&&!blocked.replacesNative&&blocked.hiddenOverflow==7,
          "zero drawable replacement cards never suppress a recoverable original HUD");
    roster=Roster(8);const auto complete=PlanLayout(roster,Native(1),cfg,960,480,true);
    Check(complete.replacesNative&&Ids(complete)==std::vector<int>{2,3,4,5,6,7,8,9}&&complete.hiddenOverflow==0,
          "overflow off still rebuilds every actual teammate when the original HUD is replaced");
    const auto all=PlanLayout(Roster(31),Native(),cfg,960,480,true);
    Check(all.replacesNative&&all.cards.size()==31&&all.hiddenOverflow==0&&all.cards.back().ordinal==31&&
          Safe(all,Roster(31),{},960,480),"replacement with overflow off rebuilds all thirty-one teammates with safe automatic wrapping");
    cfg.overflow=true;
    Check(Ids(PlanLayout(Roster(31),Native(),cfg,960,480,true))==Ids(all),
          "the overflow setting does not change which teammates replacement owns");
    cfg.singlePlayerTest=true;
    const auto single=PlanLayout(Roster(3),Native(1),cfg,960,480,true);
    Check(single.replacesNative&&Ids(single)==std::vector<int>{2,3,4},
          "replacement and single-player test together still draw each real teammate once without requiring three original sources");
    auto observer=Roster(7);observer.viewIndex=5;observer.players[2].alive=false;observer.players[3].incapacitated=true;
    const auto observed=PlanLayout(observer,Native(1),cfg,960,480,true);
    Check(Ids(observed)==std::vector<int>{2,3,4,6,7,8}&&Safe(observed,observer,{},960,480),
          "replacement preserves downed and dead identities while excluding both local and observed survivors");
    cfg.enabled=false;Check(!PlanLayout(roster,Native(1),cfg,960,480,true).replacesNative,"disabled extension never requests native replacement");
    cfg.enabled=true;roster.inGame=false;
    Check(!PlanLayout(roster,Native(1),cfg,960,480,true).replacesNative,"outside a live map replacement cannot hide native cards");
    Check(!PlanLayout(Roster(0),Native(1),cfg,960,480,true).replacesNative,"an empty teammate roster has no replacement ownership");
}
void CheckReplacementPreservesHighNativeIds() {
    auto cfg=Config();cfg.overflow=false;
    for(const auto theme:{HudTheme::Current,HudTheme::GameHud}) {
        cfg.theme=theme;auto roster=Roster(11);auto native=Native();
        for(std::size_t n=0;n<native.size();++n)native[n].playerIndex=9+static_cast<int>(n);
        const auto replaced=PlanLayout(roster,native,cfg,960,480,true);
        Check(replaced.replacesNative&&Ids(replaced)==std::vector<int>{2,3,4,5,6,7,8,9,10,11,12}&&
              replaced.hiddenOverflow==0&&replaced.cards.back().ordinal==11&&Safe(replaced,roster,{},960,480),
              "overflow-off replacement preserves high-ID teammates that would disappear when their original cards are hidden");
        Check(replaced.cards.front().bounds.x==0&&replaced.cards.back().row>0,
              "the full replacement roster starts at the lower left and wraps upward instead of applying the seven-card cap");
        std::reverse(roster.players.begin(),roster.players.end());std::reverse(native.begin(),native.end());
        const auto reordered=PlanLayout(roster,native,cfg,960,480,true);
        Check(Ids(reordered)==Ids(replaced)&&reordered.hiddenOverflow==0,
              "replacement identity order stays stable when roster and original source enumeration change");
        const auto supplement=PlanLayout(roster,native,cfg,960,480);
        Check(!supplement.replacesNative&&supplement.nativeCount==4&&Ids(supplement)==std::vector<int>{2,3,4}&&
              supplement.hiddenOverflow==4&&Safe(supplement,roster,native,960,480),
              "normal overflow-off supplementation keeps four original cards and the existing three-card addition limit");
    }
}
Snapshot UniqueSurvivors(int count) {
    auto snapshot=Roster(count-1);
    for(std::size_t n=0;n<snapshot.players.size();++n)snapshot.players[n].character=static_cast<int>(n);
    return snapshot;
}
void CheckReplacementPolicy() {
    auto healthy=UniqueSurvivors(4),duplicate=healthy;duplicate.players[1].character=0;
    Check(ObserveReplacement(duplicate).reason==ReplacementReason::DuplicateCharacter,
          "a second Nick character selects replacement independently of which player is local");
    auto names=healthy;
    for(auto& player:names.players){player.name="same nickname";player.modelPortrait.modelId=77;player.modelPortrait.revision=55;}
    Check(ObserveReplacement(names).reason==ReplacementReason::None&&ObserveReplacement(names).healthyUnique,
          "identical player nicknames and Neko model IDs never trigger character replacement");
    Check(ObserveReplacement(UniqueSurvivors(8)).reason==ReplacementReason::None,
          "eight distinct healthy survivor characters stay on the original HUD");
    auto nine=Roster(8);for(auto& player:nine.players)player.character=-1;
    Check(ObserveReplacement(nine).reason==ReplacementReason::LargeRoster,
          "nine actual survivors including self trigger replacement even when character metadata is unknown");
    auto noisy=UniqueSurvivors(8);auto copy=noisy.players.front();copy.character=7;noisy.players.push_back(copy);
    copy.index=0;noisy.players.push_back(copy);copy.index=33;noisy.players.push_back(copy);
    Check(ObserveReplacement(noisy).reason==ReplacementReason::None,
          "duplicate entity records and invalid indices cannot manufacture a large or duplicated character roster");
    auto unknown=healthy;unknown.players[0].character=unknown.players[1].character=-1;
    auto down=healthy;down.players[1].incapacitated=true;
    auto dead=healthy;dead.players[1].alive=false;
    auto hanging=healthy;hanging.players[1].hanging=true;
    auto partial=healthy;partial.players.pop_back();partial.rosterIncomplete=true;
    Check(ObserveReplacement(unknown).reason==ReplacementReason::None&&!ObserveReplacement(unknown).healthyUnique&&
          ObserveReplacement(down).reason==ReplacementReason::None&&ObserveReplacement(dead).reason==ReplacementReason::None,
          "unknown characters and life-state changes alone do not create a replacement trigger");
    ReplacementPolicy policy;
    Check(policy.Observe(duplicate,0,100)==ReplacementReason::DuplicateCharacter,"zero is a valid root HPanel lifetime token for policy decisions");
    for(const auto& snapshot:std::vector<Snapshot>{down,dead,hanging,unknown,partial})
        Check(policy.Observe(snapshot,0,9000)==ReplacementReason::DuplicateCharacter,
              "active replacement survives incap, death, hanging or incomplete roster information");
    Check(policy.Observe(healthy,0,10000)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(healthy,0,11999)==ReplacementReason::DuplicateCharacter,
          "healthy unique recovery must remain stable for the full two-second grace period");
    Check(policy.Observe(healthy,0,12000)==ReplacementReason::None,"healthy unique recovery releases replacement at exactly two seconds");
    policy.Observe(duplicate,7,0);policy.Observe(healthy,7,100);policy.Observe(down,7,2099);
    Check(policy.Observe(healthy,7,2100)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(healthy,7,4099)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(healthy,7,4100)==ReplacementReason::None,
          "an intervening unhealthy observation restarts the continuous recovery timer");
    const auto maximum=(std::numeric_limits<std::uint32_t>::max)();
    policy.Reset();policy.Observe(duplicate,8,maximum-2000);policy.Observe(healthy,8,maximum-1000);
    Check(policy.Observe(healthy,8,998)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(healthy,8,999)==ReplacementReason::None,"the two-second recovery window remains correct across DWORD clock wrap");
    policy.Observe(duplicate,9,100);
    Check(policy.Observe(healthy,10,101)==ReplacementReason::None,"a new root lifetime cannot inherit the previous HUD's replacement state");
    policy.Observe(duplicate,10,200);policy.Reset();
    Check(policy.Observe(healthy,10,201)==ReplacementReason::None,"explicit reset restores normal policy without waiting for recovery");
    policy.Observe(duplicate,10,300);
    Check(policy.Observe(healthy,0xffffffffu,99999)==ReplacementReason::DuplicateCharacter,
          "an unavailable root does not reinterpret an active decision using an unverified UI lifetime");
    policy.Reset();Check(policy.Observe(duplicate,0xffffffffu,100000)==ReplacementReason::None,
          "an unavailable root cannot initialize a new replacement decision");
}
void CheckNormalizedCharacterPolicy() {
    auto normalize=[](Snapshot& snapshot,int survivorSet) {
        snapshot.survivorSet=survivorSet;
        for(auto& player:snapshot.players)
            player.character=NormalizeSurvivorCharacter(player.rawCharacter,survivorSet);
    };
    auto legacy=Roster(3);
    for(std::size_t n=0;n<legacy.players.size();++n) {
        auto& player=legacy.players[n];player.rawCharacter=static_cast<int>(n);
        player.name="same nickname";player.modelPortrait.modelId=77;player.modelPortrait.revision=55;
    }
    normalize(legacy,1);
    Check(legacy.players[0].character==4&&legacy.players[1].character==5&&
          legacy.players[2].character==7&&legacy.players[3].character==6&&
          ObserveReplacement(legacy).reason==ReplacementReason::None&&ObserveReplacement(legacy).healthyUnique,
          "L4D1 legacy Bill, Zoey, Louis and Francis remain unique despite identical nicknames and Neko model IDs");
    for(const auto alias:std::vector<std::pair<int,int>>{{0,4},{1,5},{2,7},{3,6}}) {
        auto duplicate=legacy;duplicate.players[0].rawCharacter=alias.first;duplicate.players[1].rawCharacter=alias.second;
        normalize(duplicate,1);
        Check(ObserveReplacement(duplicate).reason==ReplacementReason::DuplicateCharacter,
              "legacy and explicit IDs for the same L4D1 character select replacement after canonical normalization");
    }
    auto duplicate=legacy;duplicate.players[1].rawCharacter=4;normalize(duplicate,1);
    ReplacementPolicy policy;
    Check(policy.Observe(duplicate,10,100)==ReplacementReason::DuplicateCharacter,
          "raw zero and explicit four are recognized as two Bills on an L4D1 map");
    auto unique=duplicate;normalize(unique,2);
    Check(unique.players[0].character==0&&unique.players[1].character==4&&
          ObserveReplacement(unique).reason==ReplacementReason::None&&ObserveReplacement(unique).healthyUnique&&
          policy.Observe(unique,10,200)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(unique,10,2199)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(unique,10,2200)==ReplacementReason::None,
          "changing to L4D2 separates raw Nick and explicit Bill but preserves the two-second recovery hold");
    policy.Observe(duplicate,10,2300);policy.Observe(unique,10,2400);
    auto unknown=unique;normalize(unknown,0);
    Check(unknown.players[0].character==-1&&unknown.players[1].character==4&&
          policy.Observe(unknown,10,10000)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(unique,10,10001)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(unique,10,12000)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(unique,10,12001)==ReplacementReason::None,
          "unknown context cannot reinterpret ambiguous raw IDs or complete the healthy unique release window");
    policy.Observe(duplicate,10,13000);policy.Reset();
    Check(policy.Observe(unknown,10,14000)==ReplacementReason::None&&
          policy.Observe(duplicate,10,14001)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(unique,11,14002)==ReplacementReason::None,
          "explicit reset and a new HUD lifetime discard a prior normalized duplicate decision");
    auto cfg=Config();cfg.singlePlayerTest=true;
    for(const auto theme:{HudTheme::Current,HudTheme::GameHud}) {
        cfg.theme=theme;
        auto l4d2=legacy;normalize(l4d2,2);
        l4d2.players[1].health=37;l4d2.players[1].incapacitated=true;
        l4d2.players[2].alive=false;l4d2.players[2].health=0;
        l4d2.players[3].hanging=true;l4d2.players[3].health=96;
        const auto first=PlanLayout(legacy,Native(3),cfg,960,480);
        const auto second=PlanLayout(l4d2,Native(3),cfg,960,480);
        bool same=first.cards.size()==second.cards.size();
        for(std::size_t n=0;same&&n<first.cards.size();++n) {
            const auto& a=first.cards[n];const auto& b=second.cards[n];
            same=a.playerIndex==b.playerIndex&&a.ordinal==b.ordinal&&a.bounds.x==b.bounds.x&&a.bounds.y==b.bounds.y&&
                 a.bounds.w==b.bounds.w&&a.bounds.h==b.bounds.h;
        }
        Check(same&&Ids(first)==std::vector<int>{2,3,4}&&Ids(second)==std::vector<int>{2,3,4},
              "character context and down, dead or hanging state do not change real player identities or placement in either theme");
    }
}
void CheckL4d1RosterCharacterResolution() {
    constexpr std::array<const char*,8> stockModels={
        "models/survivors/survivor_gambler.mdl","models/survivors/survivor_producer.mdl",
        "models/survivors/survivor_coach.mdl","models/survivors/survivor_mechanic.mdl",
        "models/survivors/survivor_namvet.mdl","models/survivors/survivor_teenangst.mdl",
        "models/survivors/survivor_biker.mdl","models/survivors/survivor_manager.mdl"
    };
    for(std::size_t n=0;n<stockModels.size();++n)
        Check(StockSurvivorCharacter(stockModels[n])==static_cast<int>(n),
              "each exact stock model identifies its canonical survivor");
    Check(StockSurvivorCharacter("MODELS\\SURVIVORS\\Survivor_Manager.MDL")==7,
          "stock model recognition accepts Windows separators and casing");
    Check(StockSurvivorCharacter("models/l4n/custom/survivor_gambler.mdl")==-1&&
          StockSurvivorCharacter("models/survivors/survivor_gambler.mdl.bak")==-1,
          "a custom path or partial stock filename cannot establish identity");

    const auto setIdentity=[](Player& player,int raw,int stock) {
        player.rawCharacter=raw;
        player.character=NormalizeSurvivorCharacter(raw,1);
        player.stockModelCharacter=stock;
    };
    auto mixed=Roster(7);
    mixed.survivorSet=1;
    constexpr std::array<int,8> loggedRaw={1,3,4,5,6,0,7,2};
    for(std::size_t n=0;n<mixed.players.size();++n) {
        const int raw=loggedRaw[n];
        setIdentity(mixed.players[n],raw,raw==0?-1:raw);
    }
    ResolveL4d1RosterCharacters(mixed.players);
    Check(std::equal(mixed.players.begin(),mixed.players.end(),loggedRaw.begin(),
                     [](const Player& player,int expected){return player.character==expected;})&&
          ObserveReplacement(mixed).reason==ReplacementReason::None&&ObserveReplacement(mixed).healthyUnique,
          "the logged eight-ID L4D1 roster retains all characters with consistent stock evidence");

    auto legacy=Roster(3);
    constexpr std::array<int,4> legacyCharacters={4,5,7,6};
    for(std::size_t n=0;n<legacy.players.size();++n)
        setIdentity(legacy.players[n],static_cast<int>(n),n==1?-1:legacyCharacters[n]);
    ResolveL4d1RosterCharacters(legacy.players);
    Check(std::equal(legacy.players.begin(),legacy.players.end(),legacyCharacters.begin(),
                     [](const Player& player,int expected){return player.character==expected;}),
          "an ordinary legacy roster retains Bill, Zoey, Louis, Francis in legacy order");

    auto conflicting=Roster(4);
    setIdentity(conflicting.players[0],0,0);
    setIdentity(conflicting.players[1],1,5);
    setIdentity(conflicting.players[2],2,-1);
    setIdentity(conflicting.players[3],3,4);
    setIdentity(conflicting.players[4],7,2);
    ResolveL4d1RosterCharacters(conflicting.players);
    constexpr std::array<int,5> expectedConflict={0,5,7,6,7};
    Check(std::equal(conflicting.players.begin(),conflicting.players.end(),expectedConflict.begin(),
                     [](const Player& player,int expected){return player.character==expected;}),
          "conflicting hints retain verified identities and leave opaque or unrelated models on the legacy fallback");
    auto opaque=Roster(1);
    setIdentity(opaque.players[0],0,-1);
    setIdentity(opaque.players[1],3,-1);
    ResolveL4d1RosterCharacters(opaque.players);
    Check(opaque.players[0].character==4&&opaque.players[1].character==6,
          "custom models alone cannot change the established legacy fallback");
}
void CheckL4d1SetReplacementPolicy() {
    Check(Settings{}.replaceL4d1Team,"L4D1 teammate replacement is enabled in the default settings");
    Check(std::string(ReplacementReasonText(ReplacementReason::L4d1SurvivorSet))=="l4d1_survivor_set",
          "the L4D1 set override has its own explicit diagnostic reason");
    auto legacy=UniqueSurvivors(4);legacy.survivorSet=1;
    auto sequel=legacy;sequel.survivorSet=2;
    auto unknown=legacy;unknown.survivorSet=0;
    auto outside=legacy;outside.inGame=false;
    ReplacementPolicy policy;
    Check(policy.Observe(legacy,10,100)==ReplacementReason::None,
          "the three-argument policy API preserves the existing automatic-only behavior");
    Check(policy.Observe(legacy,10,101,true)==ReplacementReason::L4d1SurvivorSet,
          "the current effective L4D1 survivor set selects replacement with a healthy unique four-player roster");
    Check(policy.Observe(legacy,10,102,false)==ReplacementReason::None,
          "turning off the L4D1 option releases its own override immediately");
    Check(policy.Observe(legacy,10,103,true)==ReplacementReason::L4d1SurvivorSet&&
          policy.Observe(sequel,10,104,true)==ReplacementReason::None,
          "switching to the effective L4D2 set immediately releases a previous L4D1 override");
    Check(policy.Observe(legacy,10,105,true)==ReplacementReason::L4d1SurvivorSet&&
          policy.Observe(unknown,10,106,true)==ReplacementReason::None,
          "an unavailable effective survivor set is never guessed to be L4D1 or held as a forced reason");
    Check(policy.Observe(legacy,10,107,true)==ReplacementReason::L4d1SurvivorSet&&
          policy.Observe(outside,10,108,true)==ReplacementReason::None,
          "leaving a live map releases the L4D1-only override immediately");
    for(const auto state:{1,2,3,4}) {
        auto changed=legacy;
        if(state==1)changed.players[1].incapacitated=true;
        if(state==2)changed.players[1].alive=false;
        if(state==3)changed.players[1].hanging=true;
        if(state==4)for(auto& player:changed.players)player.character=-1;
        Check(policy.Observe(changed,10,200+state,true)==ReplacementReason::L4d1SurvivorSet,
              "L4D1 replacement does not depend on life states or readable character identities");
        Check(policy.Observe(changed,10,210+state,false)==ReplacementReason::None,
              "unhealthy or unknown characters cannot make the L4D1 override sticky after its option is disabled");
    }
    policy.Reset();
    Check(policy.Observe(legacy,0xffffffffu,300,true)==ReplacementReason::None,
          "an unverified root cannot initialize the L4D1 replacement override");
    Check(policy.Observe(legacy,0,301,true)==ReplacementReason::L4d1SurvivorSet,
          "zero remains a valid verified root token for the L4D1 override");
    Check(policy.Observe(legacy,0xffffffffu,302,true)==ReplacementReason::None,
          "a missing root does not keep the L4D1-only override active");
    policy.Reset();
    Check(policy.Observe(sequel,0,303,true)==ReplacementReason::None,
          "explicit reset cannot transfer a forced L4D1 result into a different set");
    auto duplicate=legacy;duplicate.players[1].character=duplicate.players[0].character;
    Check(policy.Observe(duplicate,20,1000,true)==ReplacementReason::L4d1SurvivorSet&&
          policy.Observe(duplicate,20,1001,false)==ReplacementReason::DuplicateCharacter,
          "a duplicate trigger stays independently active underneath the selectable L4D1 override");
    Check(policy.Observe(legacy,20,1100,true)==ReplacementReason::L4d1SurvivorSet&&
          policy.Observe(legacy,20,3099,false)==ReplacementReason::DuplicateCharacter&&
          policy.Observe(legacy,20,3100,false)==ReplacementReason::None,
          "automatic duplicate recovery continues its original timer while the L4D1 override is selected");
    Check(policy.Observe(duplicate,20,3200,true)==ReplacementReason::L4d1SurvivorSet&&
          policy.Observe(unknown,20,3201,true)==ReplacementReason::DuplicateCharacter,
          "an unknown survivor set releases only the set override and preserves the real automatic duplicate hold");
    Check(policy.Observe(legacy,21,3202,false)==ReplacementReason::None,
          "a new root lifetime cannot inherit the prior automatic hold or a stale set override");
    auto nine=Roster(8);nine.survivorSet=1;
    Check(policy.Observe(nine,30,4000,true)==ReplacementReason::L4d1SurvivorSet&&
          policy.Observe(nine,30,4001,false)==ReplacementReason::LargeRoster,
          "the nine-survivor rule remains active when the L4D1-specific option is disabled");
    nine.survivorSet=2;
    Check(policy.Observe(nine,30,4002,true)==ReplacementReason::LargeRoster&&
          policy.Observe(legacy,0xffffffffu,4003,true)==ReplacementReason::LargeRoster,
          "set changes and unavailable roots cannot erase an independently active automatic large-roster decision");

    policy.Reset();auto cfg=Config();
    const bool replace=policy.Observe(legacy,40,5000,cfg.replaceL4d1Team)!=ReplacementReason::None;
    legacy.hasSelfHudBounds=true;legacy.selfHudBounds={760,400,200,80};
    const auto layout=PlanLayout(legacy,Native(3),cfg,960,480,replace);
    Check(layout.replacesNative&&Ids(layout)==std::vector<int>({2,3,4})&&Safe(layout,legacy,{},960,480)&&
          std::none_of(layout.cards.begin(),layout.cards.end(),[&](const Placement& card){return Overlap(card.bounds,legacy.selfHudBounds);}),
          "L4D1 replacement draws each actual teammate once while reserving the local player's own HUD");
    cfg.enabled=false;
    Check(!PlanLayout(legacy,Native(3),cfg,960,480,replace).replacesNative,
          "the disabled extension cannot acquire native HUD ownership through the L4D1 option");
    cfg.enabled=true;
    Check(!PlanLayout(legacy,{},cfg,960,480,replace).replacesNative,
          "a forced L4D1 reason cannot bypass the existing requirement for usable native sizing sources");
}
void CheckAllReplacementRosters() {
    for(const auto theme:{HudTheme::Current,HudTheme::GameHud})for(const auto screen:
        std::vector<std::pair<int,int>>{{320,240},{960,480}})for(int sources=1;sources<=4;++sources)
        for(int teammates=1;teammates<=31;++teammates) {
            auto cfg=Config();cfg.theme=theme;cfg.overflow=true;cfg.gap=3;cfg.rowGap=4;
            auto roster=Roster(teammates);roster.hasSelfHudBounds=true;
            roster.selfHudBounds={screen.first-100,screen.second-80,100,80};
            const auto result=PlanLayout(roster,Native(sources,50,32,screen.second),cfg,screen.first,screen.second,true);
            bool order=true;
            for(std::size_t n=0;n<result.cards.size();++n)order=order&&result.cards[n].playerIndex==2+static_cast<int>(n)&&result.cards[n].ordinal==1+static_cast<int>(n);
            Check(!result.suppressed&&result.replacesNative==!result.cards.empty()&&order&&
                  result.cards.size()+result.hiddenOverflow==static_cast<std::size_t>(teammates)&&
                  Safe(result,roster,{},screen.first,screen.second),
                  "all replacement roster sizes and one-to-four sources preserve full identity order and screen-safe packing");
        }
}
} // namespace
int main() {
    CheckIdentitiesAndCapacity();CheckFirstFitGeometry();CheckSelfHudFallback();CheckSizingAndGuards();CheckNativeTakeoverTransition();CheckSinglePlayerAndIdentity();CheckAllRosterSizes();
    CheckReplacementLayout();CheckReplacementPreservesHighNativeIds();CheckReplacementPolicy();CheckNormalizedCharacterPolicy();CheckL4d1RosterCharacterResolution();CheckL4d1SetReplacementPolicy();CheckAllReplacementRosters();
    std::cout<<"Layout/policy: "<<checks<<" checks passed (supplement and full replacement; lower-left first-fit).\n";
    return 0;
}
