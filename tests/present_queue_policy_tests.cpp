#include "udlss/present_queue_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    PresentQueueProofState s{};
    assert(!presentQueueTrusted(s,1000));

    observePresentQueueEvidence(s,0x1111,1000,false);
    assert(!presentQueueTrusted(s,1000));
    observePresentQueueEvidence(s,0x1111,1016,false);
    assert(presentQueueTrusted(s,1016));
    assert(s.trustedQueueId==0x1111);

    PresentQueueProofState mismatch{};
    observePresentQueueEvidence(mismatch,0x1111,1000,false);
    observePresentQueueEvidence(mismatch,0x2222,1016,false);
    assert(!presentQueueTrusted(mismatch,1016));
    assert(mismatch.consecutiveEvidence==1);
    observePresentQueueEvidence(mismatch,0x2222,1032,false);
    assert(presentQueueTrusted(mismatch,1032));
    assert(mismatch.trustedQueueId==0x2222);

    PresentQueueProofState exact{};
    observePresentQueueEvidence(exact,0x3333,1000,true);
    assert(presentQueueTrusted(exact,500000));
    assert(exact.creationProven);

    PresentQueueProofState expires{};
    observePresentQueueEvidence(expires,0x4444,1000,false);
    observePresentQueueEvidence(expires,0x4444,1016,false);
    assert(presentQueueTrusted(expires,4000));
    assert(!presentQueueTrusted(expires,8000));
    return 0;
}
