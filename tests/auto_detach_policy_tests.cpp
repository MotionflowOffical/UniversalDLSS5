#include "udlss/auto_detach_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
 AutoDetachState s{};
 assert(!shouldAutoDetach(s,10000,true,10000));
 s.everSawGameWindow=true;
 s.lastGameWindowTickMs=11000;
 assert(!shouldAutoDetach(s,12000,true,11900));
 assert(!shouldAutoDetach(s,12000,false,11900));
 assert(!shouldAutoDetach(s,16000,false,13050));
 assert(shouldAutoDetach(s,19000,false,13050));
 assert(!shouldAutoDetach(s,19000,true,1000));
 AutoDetachState beforePresent{};beforePresent.everSawGameWindow=true;beforePresent.lastGameWindowTickMs=20000;
 assert(!shouldAutoDetach(beforePresent,24000,false,0));
 assert(shouldAutoDetach(beforePresent,26000,false,0));
 return 0;
}
