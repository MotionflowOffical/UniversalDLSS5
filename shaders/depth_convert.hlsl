Texture2D<float> DepthIn:register(t0);RWTexture2D<float> DepthOut:register(u0);
cbuffer Params:register(b0){uint Width,Height,Downsample,Radius;float Exposure,MotionScale,ConfidenceThreshold,TextProtection;float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength;float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength;uint InvertY,HasHistory,Pad0,Pad1;float StaticDeadzone,MotionScaleX,MotionScaleY,DebugSplit;uint DebugView,DepthMode,ForceReset,UseControlMask;};
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
 if(id.x>=Width||id.y>=Height)return;
 uint sw,sh;DepthIn.GetDimensions(sw,sh);if(sw==0||sh==0){DepthOut[id.xy]=0;return;}
 uint2 src=min(uint2(sw-1,sh-1),uint2((id.x+0.5)*(float)sw/(float)Width,(id.y+0.5)*(float)sh/(float)Height));
 DepthOut[id.xy]=saturate(DepthIn.Load(int3(src,0)));
}
