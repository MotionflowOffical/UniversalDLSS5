#include "udlss/hook_quiescence.hpp"
#include "udlss/renderer_route_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    static_assert(detachDecision(true,true)==DetachMode::Unloaded);
    static_assert(detachDecision(false,true)==DetachMode::DetachedResident);
    static_assert(detachDecision(true,false)==DetachMode::DetachedResident);
    static_assert(detachDecision(false,false)==DetachMode::DetachedResident);
    return 0;
}
