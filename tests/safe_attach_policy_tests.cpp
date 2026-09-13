#include "udlss/safe_attach_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    SafeAttachState d12{1000,8,true};
    assert(!safeAttachReady(d12,1200,true));
    assert(safeAttachReady(d12,1350,true));
    d12.queueCaptured=false;assert(!safeAttachReady(d12,2000,true));
    SafeAttachState d11{1000,3,false};
    assert(!safeAttachReady(d11,1100,false));
    assert(safeAttachReady(d11,1120,false));
    return 0;
}
