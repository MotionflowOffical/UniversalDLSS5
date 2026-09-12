Texture2D<int2> NvFlow:register(t0);
Texture2D<uint> NvCost:register(t1);
RWTexture2D<float4> Flow:register(u0);
cbuffer Params:register(b0){uint Width,Height,Downsample,Radius;float Exposure,MotionScale,ConfidenceThreshold,TextProtection;float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength;float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength;uint InvertY,HasHistory,Pad0,Pad1;float StaticDeadzone,MotionScaleX,MotionScaleY,DebugSplit;uint DebugView,DepthMode,ForceReset,UseControlMask;};
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){uint lw=(Width+Downsample-1)/Downsample,lh=(Height+Downsample-1)/Downsample;if(id.x>=lw||id.y>=lh)return;if(!HasHistory){Flow[id.xy]=float4(0,0,0,1);return;}int2 raw=NvFlow.Load(int3(id.xy,0));float2 v=float2(raw)/32.0;float conf=1.0;if(Pad1!=0){float cost=(float)NvCost.Load(int3(id.xy,0));conf=saturate(1.0-cost/255.0);}Flow[id.xy]=float4(v,conf,1.0-conf);}
