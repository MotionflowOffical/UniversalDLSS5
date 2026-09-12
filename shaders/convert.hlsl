Texture2D<float4> Src : register(t0);
RWTexture2D<float4> Dst : register(u0);
cbuffer Params : register(b0) { uint Width,Height,Downsample,Radius; float Exposure,MotionScale,ConfidenceThreshold,TextProtection; float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength; float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength; uint InvertY,HasHistory,Pad0,Pad1; };
float ToSrgb(float x){x=max(x,0.0);return x<=0.0031308?12.92*x:1.055*pow(x,1.0/2.4)-0.055;}
float3 LinearToSrgb(float3 c){return float3(ToSrgb(c.r),ToSrgb(c.g),ToSrgb(c.b));}
[numthreads(8,8,1)] void CSMain(uint3 id:SV_DispatchThreadID){
 if(id.x>=Width||id.y>=Height)return;
 float4 c=Src.Load(int3(id.xy,0));
 float3 rgb=max(c.rgb*Exposure,0);
 // Pad0 marks the linear-working -> neural-proxy conversion and Pad1 says
 // the source swapchain was an sRGB typed resource.
 if(Pad0!=0&&Pad1!=0)rgb=LinearToSrgb(rgb);
 Dst[id.xy]=float4(rgb,c.a);
}
