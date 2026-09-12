cbuffer CameraMotionParams : register(b0)
{
    row_major float4x4 CurrentClipToPreviousClip;
    uint Width;
    uint Height;
    float MotionScaleX;
    float MotionScaleY;
};
Texture2D<float> DepthTex : register(t0);
RWTexture2D<float2> MotionOut : register(u0);
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if(id.x>=Width || id.y>=Height) return;
    float depth=DepthTex.Load(int3(id.xy,0));
    float2 currentPixel=float2(id.xy)+0.5;
    float2 currentNdc=float2(currentPixel.x/Width*2.0-1.0,1.0-currentPixel.y/Height*2.0);
    float4 prevClip=mul(CurrentClipToPreviousClip,float4(currentNdc,depth,1.0));
    if(abs(prevClip.w)<1e-6){MotionOut[id.xy]=0.0;return;}
    float2 prevNdc=prevClip.xy/prevClip.w;
    float2 prevPixel=float2((prevNdc.x+1.0)*0.5*Width,(1.0-prevNdc.y)*0.5*Height);
    float2 mv=(prevPixel-currentPixel)*float2(MotionScaleX,MotionScaleY);
    if(any(isnan(mv))||any(isinf(mv))||any(abs(mv)>32768.0)) mv=0.0;
    MotionOut[id.xy]=mv;
}
