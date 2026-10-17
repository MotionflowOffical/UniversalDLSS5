Texture2D<float4> Neural:register(t0);Texture2D<float4> Current:register(t1);Texture2D<float4> Mask:register(t2);Texture2D<float2> Motion:register(t3);Texture2D<float> Depth:register(t4);RWTexture2D<float4> Out:register(u0);
cbuffer Params:register(b0){uint Width,Height,Downsample,Radius;float Exposure,MotionScale,ConfidenceThreshold,TextProtection;float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength;float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength;uint InvertY,HasHistory,Pad0,Pad1;float StaticDeadzone,MotionScaleX,MotionScaleY,DebugSplit;uint DebugView,DepthMode,SourceSrgb,UseControlMask;uint ColorEncoding,HdrActive;float HdrPaperWhite,HdrMaxNits;};
float FromSrgb(float x){x=max(x,0.0);return x<=0.04045?x/12.92:pow((x+0.055)/1.055,2.4);}
float3 SrgbToLinear(float3 c){return float3(FromSrgb(c.r),FromSrgb(c.g),FromSrgb(c.b));}
float3 HdrToProxy(float3 scRgb){
    float3 positive=max(scRgb,0.0);float peak=max(positive.r,max(positive.g,positive.b));
    // Max-channel compression preserves hue without clipping saturated colors.
    return positive/(peak+max(HdrPaperWhite/80.0,0.25));
}
float NitsToPq(float n){const float m1=0.1593017578125,m2=78.84375,c1=0.8359375,c2=18.8515625,c3=18.6875;float y=pow(saturate(n/10000.0),m1);return pow((c1+c2*y)/(1.0+c3*y),m2);}
float3 Rec709To2020(float3 c){return float3(0.627404*c.r+0.329283*c.g+0.043313*c.b,0.069097*c.r+0.919541*c.g+0.011362*c.b,0.016391*c.r+0.088013*c.g+0.895595*c.b);}
float3 EncodeOutput(float3 scRgb){
    if(ColorEncoding==2){float3 nits=max(Rec709To2020(scRgb)*80.0,0.0);float peak=max(HdrMaxNits,HdrPaperWhite);nits=min(nits,float3(peak,peak,peak));return float3(NitsToPq(nits.r),NitsToPq(nits.g),NitsToPq(nits.b));}
    return scRgb;
}
float3 NeuralProxy(int2 p){float3 c=Neural.Load(int3(p,0)).rgb;return HdrActive!=0||SourceSrgb!=0?SrgbToLinear(c):c;}
float3 DebugToScene(float3 v){return HdrActive!=0?v*max(HdrPaperWhite/80.0,1.0):v;}
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
 if(id.x>=Width||id.y>=Height)return;int2 p=id.xy;float4 rawN=Neural.Load(int3(p,0)),c=Current.Load(int3(p,0)),m=Mask.Load(int3(p,0));
 float3 base=HdrActive!=0?HdrToProxy(c.rgb):c.rgb;float3 n=HdrActive!=0||SourceSrgb!=0?SrgbToLinear(rawN.rgb):rawN.rgb;
 int2 l=int2(max(p.x-1,0),p.y),r=int2(min(p.x+1,(int)Width-1),p.y),u=int2(p.x,max(p.y-1,0)),d=int2(p.x,min(p.y+1,(int)Height-1));
 float3 blur=(NeuralProxy(l)+NeuralProxy(r)+NeuralProxy(u)+NeuralProxy(d))*.25;
 // With trusted native motion, NR already handles temporal history/reset.
 // Source reactivity can jump on jitter or a history reset while the current
 // neural image stays stable. Applying it again as output opacity makes
 // lights pulse between the source and neural treatment.
 // Flag 0x4 denotes actual native vectors, distinct from the mask shader's
 // camera-inclusive confidence marker (2).
 float reactiveBlend=(Pad1&4u)!=0?0.0:m.y;
 float3 sharp=n+(n-blur)*Sharpness;float mixN=saturate(TemporalStrength*(1-reactiveBlend*HistoryClamp));float3 processedProxy=lerp(base,sharp,mixN);processedProxy=lerp(processedProxy,base,saturate(m.a));
 float3 processed;
 if(HdrActive!=0){
    // Reconstruct a residual in source scene units. Unlike multiplicative
    // RGB gain, this preserves identity exactly and can add neural detail to
    // black/shadow pixels. Keep original HDR energy rather than inverting a
    // quantized 8-bit proxy near its singular highlight endpoint.
    float peak=max(c.r,max(c.g,c.b));float paper=max(HdrPaperWhite/80.0,0.25);
    float3 residual=(processedProxy-base)*(peak+paper);
    float limit=max(peak,0.0)*0.5+paper*0.25;
    processed=max(c.rgb+clamp(residual,-limit,limit),0.0);
 }else processed=processedProxy;
 float3 outc=processed;
 if(DebugView==1)outc=c.rgb;
 else if(DebugView==2)outc=(id.x<(uint)(DebugSplit*Width))?c.rgb:processed;
 else if(DebugView==3)outc=DebugToScene(saturate(abs(processedProxy-base)*10.0));
 else if(DebugView==4){float2 mv=Motion.Load(int3(p,0));outc=DebugToScene(float3(saturate(0.5+mv.x/32.0),saturate(0.5+mv.y/32.0),0.5));}
 else if(DebugView==5){float conf=1.0-m.b;outc=DebugToScene(float3(conf,conf,conf));}
 else if(DebugView==6)outc=DebugToScene(float3(m.r,m.r,m.r));
 else if(DebugView==7){float z=Depth.Load(int3(p,0));outc=DebugToScene(float3(z,z,z));}
 else if(DebugView==8)outc=DebugToScene(n);
 Out[id.xy]=float4(EncodeOutput(outc),c.a);
}
