Texture2D<float4> Flow:register(t0);RWTexture2D<float2> Motion:register(u0);
cbuffer Params:register(b0){uint Width,Height,Downsample,Radius;float Exposure,MotionScale,ConfidenceThreshold,TextProtection;float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength;float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength;uint InvertY,HasHistory,Pad0,Pad1;float StaticDeadzone,MotionScaleX,MotionScaleY,DebugSplit;uint DebugView,DepthMode,ForceReset,UseControlMask;};
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
    if(id.x>=Width||id.y>=Height)return;
    if(Pad0!=0){Motion[id.xy]=float2(0,0);return;}
    uint2 q=id.xy/Downsample;float4 f=Flow.Load(int3(q,0));
    float2 v=f.xy*MotionScale;
    if(InvertY)v.y=-v.y;
    if(f.z<ConfidenceThreshold || length(v)<StaticDeadzone)v=float2(0,0);
    Motion[id.xy]=v;
}
