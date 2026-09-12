Texture2D<float4> Src : register(t0);
RWTexture2D<float> Dst : register(u0);
cbuffer Params : register(b0) {
    uint Width,Height,Downsample,Radius;
    float Exposure,MotionScale,ConfidenceThreshold,TextProtection;
    float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength;
    float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength;
    uint InvertY,HasHistory,Pad0,Pad1;
};
float lum(float3 c){return dot(c,float3(.2126,.7152,.0722));}
[numthreads(8,8,1)] void CSMain(uint3 id:SV_DispatchThreadID){
    uint lw=(Width+Downsample-1)/Downsample, lh=(Height+Downsample-1)/Downsample;
    if(id.x>=lw||id.y>=lh)return;
    uint2 base=id.xy*Downsample;
    uint half=max(1u,Downsample/2u);
    uint2 p0=min(base+uint2(half/2,half/2),uint2(Width-1,Height-1));
    uint2 p1=min(base+uint2(Downsample-1,half/2),uint2(Width-1,Height-1));
    uint2 p2=min(base+uint2(half/2,Downsample-1),uint2(Width-1,Height-1));
    uint2 p3=min(base+uint2(Downsample-1,Downsample-1),uint2(Width-1,Height-1));
    float l=(lum(Src.Load(int3(p0,0)).rgb)+lum(Src.Load(int3(p1,0)).rgb)+lum(Src.Load(int3(p2,0)).rgb)+lum(Src.Load(int3(p3,0)).rgb))*.25;
    Dst[id.xy]=l;
}
