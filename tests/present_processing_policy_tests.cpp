#include <cassert>

#if __has_include("udlss/present_processing_policy.hpp")
#include "udlss/present_processing_policy.hpp"
using namespace udlss;
int main(){
    assert(shouldProcessPresentBeforeMutation(false,false,false));
    assert(!shouldProcessPresentBeforeMutation(true,false,false));
    assert(!shouldProcessPresentBeforeMutation(false,true,false));
    assert(!shouldProcessPresentBeforeMutation(false,false,true));
    assert(shouldClearPresentSuppression(0));
    assert(!shouldClearPresentSuppression(1));
    assert(shouldArmPresentSuppression(1));
    assert(!shouldArmPresentSuppression(0));
    return 0;
}
#else
int main(){ return 1; }
#endif
