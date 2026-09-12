#include "udlss/camera_motion_math.hpp"
#include <cassert>
#include <cmath>
using namespace udlss;
static bool near(float a,float b,float e=0.01f){return std::fabs(a-b)<e;}
int main(){
    Matrix4 id=identityMatrix4();
    auto r=reprojectPixelCurrentToPrevious(id,50.0f,25.0f,0.5f,100,50);
    assert(r.valid);assert(near(r.motionX,0));assert(near(r.motionY,0));

    Matrix4 shift=id; shift.m[0][3]=0.2f;
    r=reprojectPixelCurrentToPrevious(shift,50.0f,25.0f,0.5f,100,50);
    assert(r.valid);assert(near(r.motionX,10.0f,0.05f));assert(near(r.motionY,0));

    Matrix4 bad=id; bad.m[3][3]=0.0f;
    r=reprojectPixelCurrentToPrevious(bad,50.0f,25.0f,0.5f,100,50);
    assert(!r.valid);

    Matrix4 a=id; a.m[0][3]=1.0f; Matrix4 inv{};
    assert(invertMatrix4(a,inv));
    const auto prod=multiplyMatrix4(a,inv);
    assert(near(prod.m[0][0],1));assert(near(prod.m[0][3],0));assert(near(prod.m[3][3],1));
    return 0;
}
