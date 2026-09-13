// Convert a game-native velocity field into UniversalDLSS5's canonical
// current->previous pixel-vector convention while resampling internal/dynamic
// render-resolution motion into output pixel units.
cbuffer NativeMotionParams : register(b0)
{
    uint Width;
    uint Height;
    uint Encoding;
    uint InvertY;
    float MotionScale;
    float InputToPixelScaleX;
    float InputToPixelScaleY;
    float Padding;
};
Texture2D<float2> NativeMotion : register(t0);
RWTexture2D<float2> MotionOut : register(u0);
static const uint PixelCurrentToPrevious = 1;
static const uint UnityUvPreviousToCurrent = 2;
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if(id.x>=Width || id.y>=Height) return;
    uint sw,sh; NativeMotion.GetDimensions(sw,sh);
    if(sw==0 || sh==0) { MotionOut[id.xy]=0; return; }
    uint2 src=min(uint2(sw-1,sh-1),uint2((id.x+0.5)*(float)sw/(float)Width,(id.y+0.5)*(float)sh/(float)Height));
    float2 raw=NativeMotion.Load(int3(src,0));
    float2 sourcePixels=raw;
    if(Encoding==UnityUvPreviousToCurrent)
        sourcePixels=-raw*float2((float)sw,(float)sh);
    else
        sourcePixels=raw*float2(InputToPixelScaleX,InputToPixelScaleY);
    float2 v=sourcePixels*float2((float)Width/(float)sw,(float)Height/(float)sh)*MotionScale;
    if(InvertY!=0) v.y=-v.y;
    if(any(isnan(v)) || any(isinf(v)) || any(abs(v)>32768.0)) v=0.0;
    MotionOut[id.xy]=v;
}
