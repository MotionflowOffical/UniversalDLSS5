Texture2D<float4> Src : register(t0);
RWTexture2D<float4> Dst : register(u0);
cbuffer Params : register(b0) {
    uint Width,Height,Downsample,Radius;
    float Exposure,MotionScale,ConfidenceThreshold,TextProtection;
    float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength;
    float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength;
    uint InvertY,HasHistory,Pad0,Pad1;
    float StaticDeadzone,MotionScaleX,MotionScaleY,DebugSplit;
    uint DebugView,DepthMode,SourceSrgb,UseControlMask;
    uint ColorEncoding,HdrActive; float HdrPaperWhite,HdrMaxNits;
};
float ToSrgb(float x){x=max(x,0.0);return x<=0.0031308?12.92*x:1.055*pow(x,1.0/2.4)-0.055;}
float3 LinearToSrgb(float3 c){return float3(ToSrgb(c.r),ToSrgb(c.g),ToSrgb(c.b));}
float PqToNits(float v){
    const float m1=0.1593017578125,m2=78.84375,c1=0.8359375,c2=18.8515625,c3=18.6875;
    float p=pow(saturate(v),1.0/m2);float num=max(p-c1,0.0);float den=max(c2-c3*p,1e-6);
    return 10000.0*pow(num/den,1.0/m1);
}
float3 Bt2020To709(float3 c){
    return float3(1.660496*c.r-0.587656*c.g-0.072840*c.b,
                 -0.124547*c.r+1.132895*c.g-0.008348*c.b,
                 -0.018154*c.r-0.100597*c.g+1.118751*c.b);
}
float3 HdrToProxy(float3 scRgb){
    float3 positive=max(scRgb,0.0);float l=max(dot(positive,float3(.2126,.7152,.0722)),1e-6);
    float paper=max(HdrPaperWhite/80.0,0.25);float mapped=l/(l+paper);return saturate(positive*(mapped/l));
}
[numthreads(8,8,1)] void CSMain(uint3 id:SV_DispatchThreadID){
    if(id.x>=Width||id.y>=Height)return;
    float4 c=Src.Load(int3(id.xy,0));
    float3 rgb=c.rgb;
    // ColorEncoding: 0 SDR linear/UNORM, 1 SDR sRGB (already decoded by SRV),
    // 2 HDR10 PQ BT.2020, 3 scRGB linear.
    if(Pad0==0){
        if(ColorEncoding==2){float3 nits=float3(PqToNits(rgb.r),PqToNits(rgb.g),PqToNits(rgb.b));rgb=Bt2020To709(nits)/80.0;}
        rgb=max(rgb*Exposure,0.0);
    }else{
        // Neural proxy conversion. HDR remains linear/high-range in current_,
        // but Feature 18 sees a bounded paper-white display-referred proxy.
        if(HdrActive!=0)rgb=LinearToSrgb(HdrToProxy(rgb));
        else if(SourceSrgb!=0)rgb=LinearToSrgb(max(rgb,0.0));
        else rgb=max(rgb,0.0);
    }
    Dst[id.xy]=float4(rgb,c.a);
}
