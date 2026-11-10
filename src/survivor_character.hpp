#pragma once
#include "types.hpp"
#include <string_view>

namespace neh {
// Verified against client.dll's portrait selector. On the L4D1 survivor set,
// legacy slots 0..3 name Bill/Zoey/Louis/Francis; explicit IDs 4..7 stay valid.
constexpr int NormalizeSurvivorCharacter(int raw,int survivorSet) {
    if (raw<0 || raw>7) return -1;
    if (raw>=4 || survivorSet==2) return raw;
    if (survivorSet==1) {
        constexpr int legacy[]={4,5,7,6};
        return legacy[raw];
    }
    return -1;
}

constexpr int StockSurvivorCharacter(std::string_view model) {
    constexpr std::string_view prefix="models/survivors/survivor_";
    if (model.size()<=prefix.size()) return -1;
    const auto normal=[](char c) {
        if (c=='\\') return '/';
        if (c>='A' && c<='Z') return static_cast<char>(c-'A'+'a');
        return c;
    };
    for (std::size_t i=0;i<prefix.size();++i) if (normal(model[i])!=prefix[i]) return -1;
    model.remove_prefix(prefix.size());
    constexpr std::string_view names[]={"gambler.mdl","producer.mdl","coach.mdl","mechanic.mdl",
                                        "namvet.mdl","teenangst.mdl","biker.mdl","manager.mdl"};
    for (int i=0;i<8;++i) {
        if (model.size()!=names[i].size()) continue;
        std::size_t n=0;
        while (n<model.size() && normal(model[n])==names[i][n]) ++n;
        if (n==model.size()) return i;
    }
    return -1;
}

inline void ResolveL4d1RosterCharacters(std::vector<Player>& players) {
    bool canonicalEvidence=false,legacyEvidence=false;
    for (const auto& player:players) {
        const int raw=player.rawCharacter;
        if (raw<0 || raw>3) continue;
        canonicalEvidence|=player.stockModelCharacter==raw;
        legacyEvidence|=player.stockModelCharacter==NormalizeSurvivorCharacter(raw,1);
    }
    for (auto& player:players) {
        const int raw=player.rawCharacter;
        const int legacy=NormalizeSurvivorCharacter(raw,1);
        player.character=legacy;
        if (raw<0 || raw>3) continue;
        // The same wire value can name a legacy survivor or a canonical L4D2
        // survivor. An exact stock model can resolve either interpretation.
        if (player.stockModelCharacter==raw || (player.stockModelCharacter!=legacy && canonicalEvidence && !legacyEvidence))
            player.character=raw;
    }
}
} // namespace neh
