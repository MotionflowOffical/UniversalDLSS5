#include "udlss/neural_scheduler_policy.hpp"
#include <array>
#include <iostream>
using namespace udlss;
int main(){
    std::array<NeuralSlotState,8> s{};
    s[0].completionValue=5;s[1].completionValue=6;s[2].completionValue=7;
    auto d=chooseNeuralSlot(s,4,3,8);
    if(d.submitSlot!=3 || !d.grew || d.activeSlots!=4){std::cerr<<"ring did not grow under pressure\n";return 1;}
    for(int i=0;i<8;++i)s[i].completionValue=10+i;
    d=chooseNeuralSlot(s,9,8,8);
    if(!d.backpressured || d.submitSlot!=-1){std::cerr<<"saturation not reported as soft backpressure\n";return 2;}
    d=chooseNeuralSlot(s,13,8,8);
    if(d.submitSlot<0 || d.backpressured){std::cerr<<"completed slot was not reused\n";return 3;}
    return 0;
}
