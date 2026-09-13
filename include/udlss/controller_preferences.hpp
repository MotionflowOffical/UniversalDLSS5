#pragma once
#include "settings.hpp"
#include <sstream>
#include <string>
#include <string_view>

namespace udlss {

struct ControllerPreferences {
    UiTheme uiTheme{UiTheme::System};
};

inline std::string encodeControllerPreferences(const ControllerPreferences& p) {
    std::ostringstream out;
    out << "version=1\n";
    out << "uiTheme=" << static_cast<unsigned>(p.uiTheme) << '\n';
    return out.str();
}

inline bool decodeControllerPreferences(std::string_view text, ControllerPreferences& out) {
    ControllerPreferences p{};
    std::istringstream in{std::string(text)};
    std::string line;
    bool any=false;
    while(std::getline(in,line)) {
        if(line.empty() || line[0]=='#') continue;
        const auto pos=line.find('=');
        if(pos==std::string::npos) continue;
        const std::string_view key(line.data(),pos);
        const std::string_view value(line.data()+pos+1,line.size()-pos-1);
        if(key=="uiTheme") {
            try {
                const auto raw=static_cast<unsigned>(std::stoul(std::string(value)));
                p.uiTheme=raw<=2 ? static_cast<UiTheme>(raw) : UiTheme::System;
                any=true;
            } catch(...) {}
        }
    }
    out=p;
    return any;
}

} // namespace udlss
