#include <cassert>
#include "udlss/control_generation.hpp"
int main(){
    udlss::GenerationTracker tracker;
    assert(!tracker.consume(0));
    assert(tracker.consume(1));
    assert(!tracker.consume(1));
    assert(tracker.consume(2));
    assert(tracker.consume(0xffffffffu));
    assert(tracker.consume(0)); // wrap-around is still a new generation
    return 0;
}
