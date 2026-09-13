#include "udlss/nvidia_driver_policy.hpp"
#include <iostream>
using namespace udlss;
int main(){
    if(!knownDirectFeature18CrashRisk({32,0,16,1664})){std::cerr<<"616.64 risk missing\n";return 1;}
    if(!knownDirectFeature18CrashRisk({32,0,16,1686})){std::cerr<<"616.86 risk missing\n";return 2;}
    if(knownDirectFeature18CrashRisk({32,0,16,1656})){std::cerr<<"616.56 incorrectly blocked\n";return 3;}
    if(knownDirectFeature18CrashRisk({31,0,15,9999})){std::cerr<<"unrelated driver incorrectly blocked\n";return 4;}
    return 0;
}
