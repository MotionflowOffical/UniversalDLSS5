#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace udlss {

struct Matrix4 { float m[4][4]{}; };
struct ReprojectionResult { float motionX{},motionY{}; bool valid{}; };

inline Matrix4 identityMatrix4(){ Matrix4 r{}; for(int i=0;i<4;i++)r.m[i][i]=1.0f; return r; }

inline Matrix4 transposeMatrix4(const Matrix4& a){ Matrix4 r{}; for(int i=0;i<4;i++)for(int j=0;j<4;j++)r.m[i][j]=a.m[j][i]; return r; }

inline Matrix4 multiplyMatrix4(const Matrix4& a,const Matrix4& b){
    Matrix4 r{}; for(int i=0;i<4;i++)for(int j=0;j<4;j++)for(int k=0;k<4;k++)r.m[i][j]+=a.m[i][k]*b.m[k][j]; return r;
}

inline bool invertMatrix4(const Matrix4& a,Matrix4& out){
    float aug[4][8]{};
    for(int r=0;r<4;r++){for(int c=0;c<4;c++)aug[r][c]=a.m[r][c];for(int c=0;c<4;c++)aug[r][4+c]=(r==c)?1.0f:0.0f;}
    for(int c=0;c<4;c++){
        int pivot=c;float best=std::fabs(aug[c][c]);
        for(int r=c+1;r<4;r++){const float v=std::fabs(aug[r][c]);if(v>best){best=v;pivot=r;}}
        if(best<1e-8f||!std::isfinite(best))return false;
        if(pivot!=c)for(int k=0;k<8;k++)std::swap(aug[pivot][k],aug[c][k]);
        const float inv=1.0f/aug[c][c];for(int k=0;k<8;k++)aug[c][k]*=inv;
        for(int r=0;r<4;r++)if(r!=c){const float f=aug[r][c];for(int k=0;k<8;k++)aug[r][k]-=f*aug[c][k];}
    }
    for(int r=0;r<4;r++)for(int c=0;c<4;c++){out.m[r][c]=aug[r][4+c];if(!std::isfinite(out.m[r][c]))return false;}
    return true;
}

inline std::array<float,4> mulMatrixVector(const Matrix4& a,const std::array<float,4>& v){
    std::array<float,4> r{};for(int i=0;i<4;i++)for(int k=0;k<4;k++)r[i]+=a.m[i][k]*v[k];return r;
}

inline ReprojectionResult reprojectPixelCurrentToPrevious(const Matrix4& currentClipToPreviousClip,
                                                          float pixelX,float pixelY,float depth,
                                                          std::uint32_t width,std::uint32_t height){
    if(!width||!height||!std::isfinite(depth))return {};
    const float ndcX=(pixelX/(float)width)*2.0f-1.0f;
    const float ndcY=1.0f-(pixelY/(float)height)*2.0f;
    const auto q=mulMatrixVector(currentClipToPreviousClip,{ndcX,ndcY,depth,1.0f});
    if(!std::isfinite(q[3])||std::fabs(q[3])<1e-6f)return {};
    const float px=q[0]/q[3], py=q[1]/q[3];
    if(!std::isfinite(px)||!std::isfinite(py))return {};
    const float prevX=(px+1.0f)*0.5f*(float)width;
    const float prevY=(1.0f-py)*0.5f*(float)height;
    ReprojectionResult r{prevX-pixelX,prevY-pixelY,true};
    if(prevX < -0.5f*width || prevX > 1.5f*width || prevY < -0.5f*height || prevY > 1.5f*height) r.valid=false;
    return r;
}

inline bool buildCurrentClipToPreviousClip(const Matrix4& currentViewProjection,
                                           const Matrix4& previousViewProjection,
                                           Matrix4& out){
    Matrix4 inv{}; if(!invertMatrix4(currentViewProjection,inv))return false; out=multiplyMatrix4(previousViewProjection,inv); return true;
}

} // namespace udlss
