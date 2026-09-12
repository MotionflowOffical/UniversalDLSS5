#include "udlss/injection_policy.hpp"
#include <cassert>
int main(){
    using namespace udlss;
    assert(!blocksThirdPartyModules({false,false}));
    assert(blocksThirdPartyModules({true,false}));
    assert(blocksThirdPartyModules({false,true}));
    assert(blocksThirdPartyModules({true,true}));
    return 0;
}
