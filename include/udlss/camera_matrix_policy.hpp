#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace udlss {

enum class CameraMatrixKind : std::uint32_t {
    Unknown=0, View=1, Projection=2, ViewProjection=3, PreviousViewProjection=4
};

inline std::string normalizeMatrixName(std::string_view name) {
    std::string out; out.reserve(name.size());
    for(unsigned char c: name) if(std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

inline CameraMatrixKind classifyMatrixBindingName(std::string_view name) {
    const auto n=normalizeMatrixName(name);
    if(n.empty() || n.find("inverse")!=std::string::npos || n.find("inv")!=std::string::npos) return CameraMatrixKind::Unknown;
    if(n.find("previousviewprojection")!=std::string::npos || n.find("prevviewprojection")!=std::string::npos ||
       n.find("previousviewproj")!=std::string::npos || n.find("prevviewproj")!=std::string::npos ||
       n.find("prevmatrixvp")!=std::string::npos || n.find("previousmatrixvp")!=std::string::npos)
        return CameraMatrixKind::PreviousViewProjection;
    if(n=="unitymatrixvp" || n=="viewprojection" || n=="viewprojectionmatrix" || n=="viewproj" ||
       n.find("cameraviewprojection")!=std::string::npos)
        return CameraMatrixKind::ViewProjection;
    if(n=="unitymatrixv" || n=="view" || n=="viewmatrix" || n=="cameraview") return CameraMatrixKind::View;
    if(n=="unitymatrixp" || n=="projection" || n=="projectionmatrix" || n=="cameraprojection") return CameraMatrixKind::Projection;
    return CameraMatrixKind::Unknown;
}

struct MatrixCandidateStats {
    std::uint32_t drawHits{};
    std::uint32_t framesObserved{};
    bool finite{};
    bool affineLike{};
};

inline std::uint32_t matrixCandidateScore(const MatrixCandidateStats& s) {
    if(!s.finite) return 0;
    std::uint32_t score=25;
    if(s.affineLike) score+=20;
    score+=std::min<std::uint32_t>(35,s.drawHits/2);
    score+=std::min<std::uint32_t>(20,s.framesObserved*4);
    return std::min<std::uint32_t>(score,100);
}

} // namespace udlss
