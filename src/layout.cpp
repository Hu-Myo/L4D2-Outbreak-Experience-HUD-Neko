#include "types.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace neh {
namespace {
int Median(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    return values.empty() ? 0 : values[values.size()/2];
}
bool Inside(const Rect& r, int width, int height) {
    return r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 &&
        static_cast<long long>(r.x)+r.w <= width &&
        static_cast<long long>(r.y)+r.h <= height;
}
bool Overlap(const Rect& a, const Rect& b) {
    return a.w>0 && a.h>0 && b.w>0 && b.h>0 &&
        a.x < static_cast<long long>(b.x)+b.w && static_cast<long long>(a.x)+a.w > b.x &&
        a.y < static_cast<long long>(b.y)+b.h && static_cast<long long>(a.y)+a.h > b.y;
}
Rect Intersection(const Rect& a,const Rect& b) {
    if (a.w<=0 || a.h<=0 || b.w<=0 || b.h<=0) return {};
    const auto left=std::max<long long>(a.x,b.x), top=std::max<long long>(a.y,b.y);
    const auto right=std::min(static_cast<long long>(a.x)+a.w,static_cast<long long>(b.x)+b.w);
    const auto bottom=std::min(static_cast<long long>(a.y)+a.h,static_cast<long long>(b.y)+b.h);
    if (right<=left || bottom<=top) return {};
    return {static_cast<int>(left),static_cast<int>(top),static_cast<int>(right-left),static_cast<int>(bottom-top)};
}
} // namespace

Layout PlanLayout(const Snapshot& snap, const std::vector<HudSlot>& native,
                  const Settings& cfg, int screenWidth, int screenHeight,bool replaceNative) {
    Layout result;
    if (!cfg.enabled) { result.reason="disabled"; return result; }
    if (!snap.inGame) { result.suppressed=true; result.reason="not_in_game"; return result; }
    if (screenWidth < 320 || screenHeight < 200 || screenWidth>32768 || screenHeight>32768) {
        result.suppressed=true; result.reason="invalid_screen_size"; return result;
    }

    // Identity, not name/model/character, is the key. The view target is already
    // represented by the local-player panel when spectating.
    std::vector<int> roster;
    for (const auto& p : snap.players) {
        if (p.index < 1 || p.index > kMaxPlayers || p.index == snap.localIndex ||
            p.index == snap.viewIndex) continue;
        roster.push_back(p.index);
    }
    std::sort(roster.begin(), roster.end());
    roster.erase(std::unique(roster.begin(), roster.end()), roster.end());
    const bool singlePlayerTest=!replaceNative && cfg.singlePlayerTest && roster.size()==3;
    if (roster.empty()) { result.reason="no_extra_teammates"; return result; }

    std::set<int> shown;
    std::vector<Rect> nativeRects;
    std::vector<int> widths, heights;
    for (const auto& slot : native) {
        if (!slot.visible) continue;
        std::string issue;
        if (replaceNative) {
            // A damaged original HUD is a sizing source only in replacement
            // mode. Its target may be duplicated, local or outside this roster.
            if(slot.playerIndex<1 || slot.playerIndex>kMaxPlayers)issue="native_target_out_of_range";
            shown.insert(slot.playerIndex);
        } else if (!std::binary_search(roster.begin(),roster.end(),slot.playerIndex)) issue="target_not_in_teammate_roster";
        else if (!shown.insert(slot.playerIndex).second) issue="duplicate_native_target";
        // The source supplies its visible dimensions and design, independently
        // of the new cards' lower-left screen anchor.
        auto bounds=Intersection(slot.bounds,{0,0,screenWidth,screenHeight});
        if (slot.hasVisibleBounds) bounds=Intersection(bounds,slot.visibleBounds);
        if (issue.empty() && (bounds.w<=0 || bounds.h<=0)) issue="native_bounds_not_visible";
        if (issue.empty() && (bounds.w<24 || bounds.h<16)) issue="native_visible_area_too_small";
        if (!issue.empty()) {
            result.suppressed=true; result.reason="ambiguous_native_hud";
            result.detail=issue+" index="+std::to_string(slot.playerIndex);
            return result;
        }
        nativeRects.push_back(bounds);
        widths.push_back(bounds.w); heights.push_back(bounds.h);
    }
    // In replacement mode this statistic counts actual sizing sources, not
    // unique target IDs from a HUD whose identity mapping may be corrupted.
    result.nativeCount=static_cast<int>(replaceNative?nativeRects.size():shown.size());
    // Custom HUDs may expose three or four teammate targets. Always subtract
    // the actual identities; a fourth native card needs no synthetic copy.
    if (replaceNative?(nativeRects.empty() || nativeRects.size()>4):
        ((singlePlayerTest && shown.size()!=3) || shown.size()<3 || shown.size()>4)) {
        result.suppressed=true;
        result.reason=replaceNative?"one_to_four_native_sources_required":
            singlePlayerTest?"three_native_cards_required_for_test":"three_or_four_native_cards_required";
        return result;
    }
    const int nativeHeight=Median(heights);

    std::vector<int> missing;
    for (int index : roster) if (replaceNative || singlePlayerTest || shown.count(index)==0) missing.push_back(index);
    if (missing.empty()) {result.reason="no_extra_teammates";return result;}
    const float scale=std::clamp(std::isfinite(cfg.scale)?cfg.scale:0.85f,0.50f,1.20f);
    const int cardWidth=std::max(40,static_cast<int>(std::lround(Median(widths)*scale)));
    const int cardHeight=std::max(28,static_cast<int>(std::lround(nativeHeight*scale)));
    int gap=std::clamp(cfg.gap,0,40);
    if (cfg.theme==HudTheme::GameHud && nativeRects.size()>1) {
        auto ordered=nativeRects;
        std::sort(ordered.begin(),ordered.end(),[](const Rect& a,const Rect& b){return a.x<b.x;});
        std::vector<int> observed;
        for (std::size_t n=1;n<ordered.size();++n) {
            const auto& a=ordered[n-1];const auto& b=ordered[n];
            const int nativeGap=b.x-a.x-a.w;
            const bool sameRow=std::abs(a.y+a.h-b.y-b.h)<=std::max(8,std::min(a.h,b.h)/2);
            if (b.x>a.x && sameRow && nativeGap>=-40 && nativeGap<=40)observed.push_back(nativeGap);
        }
        if (!observed.empty())gap=std::max(0,Median(observed));
    }
    const int rowGap=std::clamp(cfg.rowGap,0,60);
    const int firstX=std::clamp(cfg.offsetX,0,screenWidth);
    std::size_t eligible=missing.size();
    // Replacement owns the complete teammate roster, including the hidden
    // original targets. Its capacity is screen space, never the supplement cap.
    if (!replaceNative && !singlePlayerTest && !cfg.overflow) {
        eligible=std::min(eligible,static_cast<std::size_t>(7-result.nativeCount));
        result.hiddenOverflow=static_cast<int>(missing.size()-eligible);
    }
    if (cardWidth>screenWidth-firstX || cardHeight>screenHeight) {
        result.hiddenOverflow+=static_cast<int>(eligible);
        result.reason="insufficient_screen_space";return result;
    }
    const int baseY=std::clamp(screenHeight-cardHeight+std::clamp(cfg.offsetY,-screenHeight,screenHeight),
                               0,screenHeight-cardHeight);
    std::vector<Rect> occupied=replaceNative?std::vector<Rect>{}:nativeRects;
    Rect self{};
    if (snap.hasSelfHudBounds) {
        self=Intersection(snap.selfHudBounds,{0,0,screenWidth,screenHeight});
    }
    if (self.w<=0 || self.h<=0) {
        // Keep the lower-right local HUD clear while its panel is unavailable.
        // The reserve covers the portrait, health and nearby state indicators
        // in the HUD's 480-unit proportional coordinate space.
        const int reserveWidth=std::min(screenWidth,(screenHeight*164+479)/480);
        const int reserveHeight=(screenHeight*100+479)/480;
        self={screenWidth-reserveWidth,screenHeight-reserveHeight,reserveWidth,reserveHeight};
    }
    occupied.push_back(self);
    std::size_t next=0;
    // Scan free intervals from the lower left. A blocked interval advances to
    // its right edge, rather than losing that teammate or assuming fixed 3/7
    // columns. The next row is determined by the current visible card size.
    for (int row=0,y=baseY;y>=0 && next<eligible;++row,y-=cardHeight+rowGap) {
        for (int x=firstX;x<=screenWidth-cardWidth && next<eligible;) {
            Rect rect{x,y,cardWidth,cardHeight};
            int afterBlocker=x;
            for (const auto& old:occupied)if (Overlap(rect,old))
                afterBlocker=std::max(afterBlocker,old.x+old.w+gap);
            if (afterBlocker>x) {x=afterBlocker;continue;}
            if (!Inside(rect,screenWidth,screenHeight))break;
            result.cards.push_back({missing[next],row,(replaceNative || singlePlayerTest?1:result.nativeCount+1)+static_cast<int>(next),rect});
            ++next;x+=cardWidth+gap;
        }
    }
    if (next<eligible) {result.hiddenOverflow+=static_cast<int>(eligible-next);result.reason="insufficient_screen_space";}
    result.replacesNative=replaceNative && !result.cards.empty();
    if (result.reason.empty()) result.reason=replaceNative?"native_replacement":singlePlayerTest?"single_player_test":"ok";
    return result;
}
} // namespace neh
