#include "udlss/nvof_policy.hpp"
#include <cassert>
#include <array>
using namespace udlss;
int main(){
  std::array<std::uint32_t,3> caps{1,2,4};
  assert(chooseNvofGrid(1,caps)==1);
  assert(chooseNvofGrid(2,caps)==2);
  assert(chooseNvofGrid(4,caps)==4);
  assert(chooseNvofGrid(8,caps)==4);
  std::array<std::uint32_t,2> sparse{2,4};
  assert(chooseNvofGrid(1,sparse)==2);
  assert(nvofPerfClass(LatencyMode::UltraLow)==NvofPerfClass::Fast);
  assert(nvofPerfClass(LatencyMode::Balanced)==NvofPerfClass::Medium);
  assert(nvofPerfClass(LatencyMode::Quality)==NvofPerfClass::Slow);
}
