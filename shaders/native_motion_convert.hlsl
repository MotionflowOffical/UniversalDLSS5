// Convert a game-native velocity field into UniversalDLSS5's canonical
// current->previous pixel-vector convention.  Encoding 2 corresponds to
// NativeMotionEncoding::UnityUvPreviousToCurrent: Unity stores
// currentUV-previousUV, so DLSS current->previous is -raw * dimensions.
cbuffer NativeMotionParams : register(b0)
{
    uint Width;
    uint Height;
    uint Encoding;
    uint InvertY;
    float MotionScale;
    float3 Padding;
};
Texture2D<float2> NativeMotion : register(t0);
RWTexture2D<float2> MotionOut : register(u0);
static const uint UnityUvPreviousToCurrent = 2;
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if(id.x>=Width || id.y>=Height) return;
    float2 raw=NativeMotion.Load(int3(id.xy,0));
    float2 v=raw;
    if(Encoding == UnityUvPreviousToCurrent)
        v=-raw*float2((float)Width,(float)Height);
    v*=MotionScale;
    if(InvertY!=0) v.y=-v.y;
    if(any(isnan(v)) || any(isinf(v)) || any(abs(v)>32768.0)) v=0.0;
    MotionOut[id.xy]=v;
}
