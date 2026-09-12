Texture2D<float4> Src : register(t0);
RWTexture2D<float4> Dst : register(u0);
cbuffer Params : register(b0) { uint Width,Height,Downsample,Radius; float Exposure,MotionScale,ConfidenceThreshold,TextProtection; float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength; float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength; uint InvertY,HasHistory,Pad0,Pad1; };
[numthreads(8,8,1)] void CSMain(uint3 id:SV_DispatchThreadID){if(id.x>=Width||id.y>=Height)return;float4 c=Src.Load(int3(id.xy,0));Dst[id.xy]=float4(max(c.rgb*Exposure,0),c.a);}
