#include "udlss/camera_matrix_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    assert(classifyMatrixBindingName("unity_MatrixV")==CameraMatrixKind::View);
    assert(classifyMatrixBindingName("UNITY_MATRIX_V")==CameraMatrixKind::View);
    assert(classifyMatrixBindingName("ViewMatrix")==CameraMatrixKind::View);
    assert(classifyMatrixBindingName("unity_MatrixP")==CameraMatrixKind::Projection);
    assert(classifyMatrixBindingName("ProjectionMatrix")==CameraMatrixKind::Projection);
    assert(classifyMatrixBindingName("unity_MatrixVP")==CameraMatrixKind::ViewProjection);
    assert(classifyMatrixBindingName("PrevViewProj")==CameraMatrixKind::PreviousViewProjection);
    assert(classifyMatrixBindingName("PreviousViewProjection")==CameraMatrixKind::PreviousViewProjection);
    assert(classifyMatrixBindingName("unity_MatrixInvV")==CameraMatrixKind::Unknown);
    assert(classifyMatrixBindingName("SomeColorMatrix")==CameraMatrixKind::Unknown);
    MatrixCandidateStats stable{}; stable.drawHits=120; stable.framesObserved=6; stable.finite=true; stable.affineLike=true;
    assert(matrixCandidateScore(stable)>=70);
    MatrixCandidateStats weak{}; weak.drawHits=1; weak.framesObserved=1; weak.finite=true; weak.affineLike=false;
    assert(matrixCandidateScore(weak)<50);
    return 0;
}
