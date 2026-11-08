#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neh {
namespace hud_panel_resource_detail {
inline char FoldAscii(char value) {
    return value>='A' && value<='Z' ? static_cast<char>(value-'A'+'a') : value;
}

inline bool EqualKey(std::string_view left,std::string_view right) {
    if (left.size()!=right.size()) return false;
    for (std::size_t i=0;i<left.size();++i)
        if (FoldAscii(left[i])!=FoldAscii(right[i])) return false;
    return true;
}
} // namespace hud_panel_resource_detail

struct HudPanelControl {
    std::string name;
    std::vector<std::pair<std::string,std::string>> fields;

    std::string_view Get(std::string_view key) const {
        const auto* value=FindValue(key);
        return value?std::string_view(*value):std::string_view{};
    }
    bool Has(std::string_view key) const {return FindValue(key)!=nullptr;}

private:
    const std::string* FindValue(std::string_view key) const {
        for (const auto& field:fields)
            if (hud_panel_resource_detail::EqualKey(field.first,key)) return &field.second;
        return nullptr;
    }
};

struct HudPanelResource {
    std::vector<HudPanelControl> controls;

    const HudPanelControl* Find(std::string_view name) const {
        for (const auto& control:controls)
            if (hud_panel_resource_detail::EqualKey(control.name,name)) return &control;
        return nullptr;
    }
};

namespace hud_panel_resource_detail {
inline constexpr std::size_t kMaxBytes=64*1024;
inline constexpr std::size_t kMaxTokenBytes=1024;
inline constexpr std::size_t kMaxTokens=8192;
inline constexpr std::size_t kMaxBlockEntries=128;
inline constexpr unsigned kMaxDepth=8;

enum class TokenKind {Text,Open,Close,Condition,End,Invalid};
struct Token {
    TokenKind kind=TokenKind::Invalid;
    std::string_view text;
};

inline bool Space(char value) {
    return value==' ' || value=='\t' || value=='\r' || value=='\n' || value=='\f' || value=='\v';
}

class Parser {
public:
    explicit Parser(std::string_view text):text_(text) {Next();}

    bool Parse(HudPanelResource& result) {
        if (!Name()) return false;
        Next();
        if (!Take(TokenKind::Open)) return false;
        std::size_t count=0;
        while (token_.kind!=TokenKind::Close) {
            if (++count>kMaxBlockEntries || !Name()) return false;
            const auto name=token_.text;
            Next();
            bool enabled=true,hasCondition=false;
            if (!Condition(enabled,hasCondition)) return false;
            if (token_.kind!=TokenKind::Open) return false;
            HudPanelControl control;
            if (!Fields(&control,2) || !Condition(enabled,hasCondition)) return false;
            if (!enabled) continue;
            if (result.Find(name)) return false;
            control.name.assign(name);
            result.controls.push_back(std::move(control));
        }
        Next();
        return token_.kind==TokenKind::End;
    }

private:
    std::string_view text_;
    std::size_t pos_=0,tokens_=0;
    Token token_;

    bool Take(TokenKind kind) {
        if (token_.kind!=kind) return false;
        Next();
        return true;
    }

    bool Name() const {
        return token_.kind==TokenKind::Text && !token_.text.empty() && token_.text.front()!='#';
    }

    bool Condition(bool& enabled,bool& alreadySeen) {
        if (token_.kind!=TokenKind::Condition) return true;
        if (alreadySeen) return false;
        alreadySeen=true;
        // The installed Windows teammate resource uses just this platform pair.
        if (EqualKey(token_.text,"[$OSX]")) enabled=false;
        else if (EqualKey(token_.text,"[!$OSX]")) enabled=true;
        else return false;
        Next();
        return true;
    }

    bool Fields(HudPanelControl* control,unsigned depth) {
        if (depth>kMaxDepth || !Take(TokenKind::Open)) return false;
        std::vector<std::string_view> nestedNames;
        std::size_t count=0;
        while (token_.kind!=TokenKind::Close) {
            if (++count>kMaxBlockEntries || !Name()) return false;
            const auto key=token_.text;
            Next();
            bool enabled=true,hasCondition=false;
            if (!Condition(enabled,hasCondition)) return false;
            if (token_.kind==TokenKind::Open) {
                if (!Fields(nullptr,depth+1) || !Condition(enabled,hasCondition)) return false;
                if (control && enabled) {
                    if (control->Has(key)) return false;
                    nestedNames.push_back(key);
                }
                continue;
            }
            if (token_.kind!=TokenKind::Text) return false;
            const auto value=token_.text;
            Next();
            if (!Condition(enabled,hasCondition)) return false;
            if (!control || !enabled) continue;
            for (const auto nested:nestedNames) if (EqualKey(nested,key)) return false;
            if (!Store(*control,key,value)) return false;
        }
        Next();
        return true;
    }

    static bool Store(HudPanelControl& control,std::string_view key,std::string_view value) {
        for (auto& field:control.fields) if (EqualKey(field.first,key)) {
            if (field.second==value) return true;
            // Allow repeated color overrides, but never guess which conflicting
            // layout, font or control declaration the active skin intended.
            if (!EqualKey(key,"fgcolor_override") && !EqualKey(key,"bgcolor_override")) return false;
            field.second.assign(value);
            return true;
        }
        control.fields.emplace_back(key,value);
        return true;
    }

    void Next() {
        token_={};
        for (;;) {
            while (pos_<text_.size() && Space(text_[pos_])) ++pos_;
            if (pos_==text_.size()) {token_.kind=TokenKind::End;return;}
            if (text_.substr(pos_,2)!="//") break;
            while (pos_<text_.size() && text_[pos_]!='\n' && text_[pos_]!='\r') ++pos_;
        }
        if (++tokens_>kMaxTokens) return;
        const char first=text_[pos_];
        if (first=='{' || first=='}') {
            ++pos_;
            token_.kind=first=='{'?TokenKind::Open:TokenKind::Close;
            return;
        }
        if (first=='"') {
            const auto start=++pos_;
            while (pos_<text_.size() && text_[pos_]!='"') {
                if (text_[pos_]=='\r' || text_[pos_]=='\n' ||
                    (text_[pos_]=='\\' && pos_+1<text_.size() && text_[pos_+1]=='"')) return;
                ++pos_;
                if (pos_-start>kMaxTokenBytes) return;
            }
            if (pos_==text_.size()) return;
            token_={TokenKind::Text,text_.substr(start,pos_-start)};
            ++pos_;
            return;
        }
        const auto start=pos_;
        while (pos_<text_.size() && !Space(text_[pos_]) &&
               text_[pos_]!='{' && text_[pos_]!='}' && text_[pos_]!='"') {
            ++pos_;
            if (pos_-start>kMaxTokenBytes) return;
        }
        const auto bare=text_.substr(start,pos_-start);
        if (bare.empty() || EqualKey(bare,"#include") || EqualKey(bare,"#base")) return;
        if (first!='[' && (bare.find('[')!=std::string_view::npos || bare.find(']')!=std::string_view::npos)) return;
        token_={first=='['?TokenKind::Condition:TokenKind::Text,bare};
    }
};
} // namespace hud_panel_resource_detail

// Parse one Windows teammate panel resource into copied values. A rejected
// resource leaves no partial controls or fields for the renderer to use.
inline bool ParseHudPanelResource(std::string_view text,HudPanelResource& output) {
    using namespace hud_panel_resource_detail;
    if (text.size()>kMaxBytes || text.find('\0')!=std::string_view::npos) {output={};return false;}
    if (text.substr(0,3)=="\xef\xbb\xbf") text.remove_prefix(3);
    HudPanelResource parsed;
    if (!Parser(text).Parse(parsed)) {output={};return false;}
    output=std::move(parsed);
    return true;
}
} // namespace neh
