Texture2D<float4> Neural:register(t0);Texture2D<float4> Current:register(t1);Texture2D<float4> Mask:register(t2);Texture2D<float2> Motion:register(t3);Texture2D<float> Depth:register(t4);RWTexture2D<float4> Out:register(u0);
cbuffer Params:register(b0){uint Width,Height,Downsample,Radius;float Exposure,MotionScale,ConfidenceThreshold,TextProtection;float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength;float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength;uint InvertY,HasHistory,Pad0,Pad1;float StaticDeadzone,MotionScaleX,MotionScaleY,DebugSplit;uint DebugView,DepthMode,SourceSrgb,UseControlMask;};
float FromSrgb(float x){x=max(x,0.0);return x<=0.04045?x/12.92:pow((x+0.055)/1.055,2.4);}
float3 SrgbToLinear(float3 c){return float3(FromSrgb(c.r),FromSrgb(c.g),FromSrgb(c.b));}
float3 NeuralRgb(int2 p){float3 c=Neural.Load(int3(p,0)).rgb;return SourceSrgb!=0?SrgbToLinear(c):c;}
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
 if(id.x>=Width||id.y>=Height)return;int2 p=id.xy;float4 rawN=Neural.Load(int3(p,0)),c=Current.Load(int3(p,0)),m=Mask.Load(int3(p,0));
 float3 n=SourceSrgb!=0?SrgbToLinear(rawN.rgb):rawN.rgb;
 int2 l=int2(max(p.x-1,0),p.y),r=int2(min(p.x+1,(int)Width-1),p.y),u=int2(p.x,max(p.y-1,0)),d=int2(p.x,min(p.y+1,(int)Height-1));
 float3 blur=(NeuralRgb(l)+NeuralRgb(r)+NeuralRgb(u)+NeuralRgb(d))*.25;
 float3 sharp=n+(n-blur)*Sharpness;float mixN=saturate(TemporalStrength*(1-m.y*HistoryClamp));float3 processed=lerp(c.rgb,sharp,mixN);processed=lerp(processed,c.rgb,saturate(m.a));
 float3 outc=processed;
 if(DebugView==1)outc=c.rgb;
 else if(DebugView==2)outc=(id.x<(uint)(DebugSplit*Width))?c.rgb:processed;
 else if(DebugView==3)outc=saturate(abs(processed-c.rgb)*10.0);
 else if(DebugView==4){float2 mv=Motion.Load(int3(p,0));outc=float3(saturate(0.5+mv.x/32.0),saturate(0.5+mv.y/32.0),0.5);}
 else if(DebugView==5){float conf=1.0-m.b;outc=conf.xxx;}
 else if(DebugView==6)outc=m.rrr;
 else if(DebugView==7){float z=Depth.Load(int3(p,0));outc=z.xxx;}
 else if(DebugView==8)outc=n;
 Out[id.xy]=float4(outc,c.a);
}
