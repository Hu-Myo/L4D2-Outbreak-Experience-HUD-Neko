#pragma once
#include <cstddef>

namespace neh {
inline bool IsKoreanLanguage(const char* language) {
    if(!language)return false;
    auto matches=[language](const char* expected) {
        for(std::size_t n=0;;++n) {
            const unsigned char current=static_cast<unsigned char>(language[n]);
            const unsigned char lower=current>='A' && current<='Z'?current+('a'-'A'):current;
            if(lower!=static_cast<unsigned char>(expected[n]))return false;
            if(!lower)return true;
        }
    };
    return matches("korean") || matches("koreana");
}
template<class ReadClient,class ReadSteam>
bool ResolveKoreanMenu(ReadClient readClient,ReadSteam readSteam) {
    char language[33]{};
    if(readClient(language) && language[0])return IsKoreanLanguage(language);
    language[0]='\0';
    return readSteam(language) && IsKoreanLanguage(language);
}
enum class MenuText:unsigned {
    Title,Extension,Overflow,Theme,GameHud,CurrentDesign,SinglePlayerTest,DiagnosticLogging,
    Reload,DiagnosticSnapshot,L4d1Replacement,On,Off,Count
};
inline const char* MenuLabel(MenuText text,bool korean) {
    struct Translation {const char* english;const char* korean;};
    static constexpr Translation labels[]={
        {"Extra Survivor HUD",u8"추가 생존자 HUD"},
        {"Extension: ",u8"추가 HUD: "},
        {"8th+ teammate cards: ",u8"8번째 이후 팀원 HUD: "},
        {"Theme: ",u8"테마: "},
        {"In-game HUD",u8"게임 내 HUD"},
        {"Current design",u8"현재 디자인"},
        {"Single-player test: ",u8"싱글 테스트: "},
        {"Diagnostic logging: ",u8"진단 로그: "},
        {"Reload extra_hud.ini",u8"extra_hud.ini 다시 불러오기"},
        {"Write diagnostic snapshot",u8"진단 정보 기록"},
        {"Replace L4D1 team HUD: ",u8"L4D1 팀원 HUD 전체 대체: "},
        {"ON",u8"켜짐"},{"OFF",u8"꺼짐"}
    };
    static_assert(sizeof(labels)/sizeof(labels[0])==static_cast<unsigned>(MenuText::Count),"Every menu label needs both translations.");
    const auto index=static_cast<unsigned>(text);
    if(index>=static_cast<unsigned>(MenuText::Count))return "";
    return korean?labels[index].korean:labels[index].english;
}
} // namespace neh
