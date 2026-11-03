#pragma once

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
} // namespace neh
