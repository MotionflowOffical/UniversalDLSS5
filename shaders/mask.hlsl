Texture2D<float4> Cur:register(t0);
Texture2D<float4> Prev:register(t1);
Texture2D<float4> Flow:register(t2);
Texture2D<float2> Motion:register(t3);
RWTexture2D<float4> Mask:register(u0);
cbuffer Params:register(b0){
    uint Width,Height,Downsample,Radius;
    float Exposure,MotionScale,ConfidenceThreshold,TextProtection;
    float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength;
    float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength;
    uint InvertY,HasHistory,Pad0,Pad1;
    float StaticDeadzone,MotionScaleX,MotionScaleY,DebugSplit;
    uint DebugView,DepthMode,ForceReset,UseControlMask;
};
float lum(float3 c){return dot(c,float3(.2126,.7152,.0722));}
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
    if(id.x>=Width||id.y>=Height)return;
    int2 p=id.xy;
    int2 l=int2(max(p.x-1,0),p.y),r=int2(min(p.x+1,(int)Width-1),p.y);
    int2 u=int2(p.x,max(p.y-1,0)),d=int2(p.x,min(p.y+1,(int)Height-1));
    float yc=lum(Cur.Load(int3(p,0)).rgb),yl=lum(Cur.Load(int3(l,0)).rgb),yr=lum(Cur.Load(int3(r,0)).rgb);
    float yu=lum(Cur.Load(int3(u,0)).rgb),yd=lum(Cur.Load(int3(d,0)).rgb);
    float gx=abs(yr-yl),gy=abs(yd-yu),edge=gx+gy;
    float lap=abs(yc*4.0-yl-yr-yu-yd);
    float neighborhoodSpan=max(max(abs(yl-yr),abs(yu-yd)),max(abs(yc-yl),abs(yc-yu)));
    float flatBackground=1.0-saturate(neighborhoodSpan*3.0);
    float3 cur=Cur.Load(int3(p,0)).rgb;

    // Compare against the PREVIOUS LOCATION of the current pixel, not the
    // same screen coordinate.  Motion is current->previous in pixel units.
    // Without this reprojection ordinary camera motion creates two residual
    // silhouettes (old and new positions), which looks exactly like ghosting
    // in the ControlMask and causes NR to discard otherwise valid history.
    float2 motion=Motion.Load(int3(p,0))*float2(MotionScaleX,MotionScaleY);
    float2 prevPosF=float2(p)+motion;
    int2 prevPos=int2(round(prevPosF));
    bool inBounds=prevPos.x>=0&&prevPos.y>=0&&prevPos.x<(int)Width&&prevPos.y<(int)Height;
    float diff=1.0;
    if(HasHistory&&inBounds) diff=length(cur-Prev.Load(int3(prevPos,0)).rgb);

    float stable=(HasHistory&&inBounds)?saturate(1.0-diff*12.0):0.0;
    float textLike=saturate((lap-EdgeThreshold*0.65)*12.0)*saturate(edge*8.0)*flatBackground*stable;
    float uiLike=saturate((edge-EdgeThreshold)*7.0)*stable;
    float protectedPixels=saturate(max(textLike*TextProtection,uiLike*UIProtection));
    uint2 flowCoord=min(id.xy/max(1u,Downsample),uint2((Width+Downsample-1)/Downsample-1,(Height+Downsample-1)/Downsample-1));
    float4 f=Flow.Load(int3(flowCoord,0));
    // Pad1==2 marks trusted adapter/native motion; optical-flow confidence
    // must not suppress a motion field that did not come from optical flow.
    float lowConf=(Pad1==2)?0.0:saturate((ConfidenceThreshold-f.z)*8.0);
    float outOfBounds=inBounds?0.0:1.0;
    float reactive=saturate(diff*ReactiveStrength*8.0+lowConf*DisocclusionThreshold+outOfBounds);
    float unreliable=saturate(max(protectedPixels,max(reactive,lowConf)));
    // Feature 18 ControlMask polarity is R=1 apply NR, R=0 bypass NR.
    float nrApplication=UseControlMask!=0?saturate(1.0-unreliable*ControlMaskStrength):1.0;
    Mask[id.xy]=float4(nrApplication,reactive,lowConf,protectedPixels);
}
