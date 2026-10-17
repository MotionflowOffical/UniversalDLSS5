#pragma once
#include <cstdint>

namespace udlss {
// Promote only completed copy slots. Publication may replace the current slot,
// so currentSequence must be read again for each candidate.
template<class Slots,class CurrentSequence,class Publish,class Retire>
void promoteCompletedSnapshotSlots(Slots& slots,std::uint64_t completed,
                                  CurrentSequence currentSequence,Publish publish,Retire retire){
    if(completed==UINT64_MAX)return;
    for(auto& slot:slots){
        if(slot.captureConsumed||!slot.fenceValue||slot.fenceValue==UINT64_MAX||
           completed<slot.fenceValue||!slot.stagedCapture)continue;
        const auto sequence=slot.stagedCapture.captureSequence;
        if(currentSequence()==sequence)continue;
        // A rejected completed generation is already superseded by the
        // current capture. Retire its slot too, including after ring wrap.
        // Outstanding reader leases still prevent physical texture reuse.
        if(!publish(slot.stagedCapture))retire(sequence);
    }
}
}
