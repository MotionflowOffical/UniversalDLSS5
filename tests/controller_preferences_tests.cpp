#include "udlss/controller_preferences.hpp"
#include <cassert>
using namespace udlss;
int main(){
    ControllerPreferences p{};p.uiTheme=UiTheme::Dark;
    const auto encoded=encodeControllerPreferences(p);
    assert(encoded.find("uiTheme=2")!=std::string::npos);
    ControllerPreferences decoded{};
    assert(decodeControllerPreferences(encoded,decoded));
    assert(decoded.uiTheme==UiTheme::Dark);
    ControllerPreferences invalid{};
    assert(decodeControllerPreferences("uiTheme=99\n",invalid));
    assert(invalid.uiTheme==UiTheme::System);
    return 0;
}
