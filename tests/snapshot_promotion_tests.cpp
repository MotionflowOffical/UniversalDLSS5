#include "udlss/snapshot_promotion.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Capture {std::uint64_t captureSequence{};explicit operator bool()const{return captureSequence!=0;}};
struct Slot {bool captureConsumed{true};std::uint64_t fenceValue{};Capture stagedCapture{};};
}
int main(){try{
    std::array<Slot,6> slots{};std::uint64_t current=1;
    slots[0]={false,1,{1}};
    auto retire=[&](std::uint64_t sequence){for(auto& slot:slots)if(slot.stagedCapture.captureSequence==sequence){slot.captureConsumed=true;slot.stagedCapture={};}};
    auto publish=[&](Capture next){if(next.captureSequence<current)return false;retire(current);current=next.captureSequence;return true;};
    auto promote=[&](std::uint64_t completed){udlss::promoteCompletedSnapshotSlots(slots,completed,[&]{return current;},publish,retire);};
    slots[1]={false,2,{2}};slots[2]={false,3,{3}};
    promote(2);check(current==2&&slots[0].captureConsumed,"completed snapshot must replace old current");
    check(!slots[2].captureConsumed&&slots[2].stagedCapture.captureSequence==3,"in-flight snapshot must be retained");
    // Freed slot zero is reused before slot two completes. Physical scan order
    // sees newest (4) before older completed (3), which must still be reclaimed.
    slots[0]={false,4,{4}};promote(4);
    check(current==4,"wrapped newer snapshot must remain current");
    check(slots[2].captureConsumed&&!slots[2].stagedCapture,"rejected older completed snapshot pins a ring slot");
    for(std::uint64_t sequence=5;sequence<20005;sequence+=2){
        std::size_t a=slots.size(),b=slots.size();
        for(std::size_t i=0;i<slots.size();++i)if(slots[i].captureConsumed){if(b==slots.size())b=i;else a=i;}
        check(a<slots.size()&&b<slots.size(),"wrapped ring has no free slots");
        slots[a]={false,sequence,{sequence}};slots[b]={false,sequence+1,{sequence+1}};
        promote(sequence+1);
        check(current==sequence+1,"current snapshot moved backwards");
        unsigned occupied=0;for(const auto& slot:slots)occupied+=!slot.captureConsumed;
        check(occupied==1,"completed superseded snapshot accumulated");
    }
    const auto before=current;slots[5]={false,UINT64_MAX,{30000}};
    promote(UINT64_MAX);check(current==before&&!slots[5].captureConsumed,"removed-device fence must not promote snapshots");
    promote(40000);check(current==before&&!slots[5].captureConsumed,"unpublished in-flight slot must remain retained");
    std::cout<<"PASS: wrapped snapshot promotion retires superseded completed slots\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
