Texture2D<float> Cur:register(t0);
Texture2D<float> Prev:register(t1);
RWTexture2D<float4> Flow:register(u0);
cbuffer Params:register(b0){
    uint Width,Height,Downsample,Radius;
    float Exposure,MotionScale,ConfidenceThreshold,TextProtection;
    float UIProtection,EdgeThreshold,Sharpness,ReactiveStrength;
    float ControlMaskStrength,HistoryClamp,DisocclusionThreshold,TemporalStrength;
    uint InvertY,HasHistory,Pad0,Pad1;
    float StaticDeadzone,MotionScaleX,MotionScaleY,DebugSplit;
    uint DebugView,DepthMode,ForceReset,UseControlMask;
};
float patch(int2 p,int2 q,int2 dim){
    float e=0;
    [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++){
        int2 a=clamp(p+int2(x,y),int2(0,0),dim-1);
        int2 b=clamp(q+int2(x,y),int2(0,0),dim-1);
        e+=abs(Cur.Load(int3(a,0))-Prev.Load(int3(b,0)));
    }
    return e/9.0;
}
float length2(int2 v){float2 f=float2(v);return dot(f,f);}
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
    uint lw=(Width+Downsample-1)/Downsample,lh=(Height+Downsample-1)/Downsample;
    if(id.x>=lw||id.y>=lh)return;
    if(!HasHistory){Flow[id.xy]=float4(0,0,0,1);return;}
    int2 dim=int2(lw,lh),p=int2(id.xy);
    // Seed with zero motion so an ambiguous/static patch never inherits the
    // first extreme search offset merely because it was visited first.
    int2 bo=int2(0,0);
    float best=patch(p,p,dim),second=1e9;
    const float eps=1e-6;
    for(int y=-(int)Radius;y<=(int)Radius;y++)for(int x=-(int)Radius;x<=(int)Radius;x++){
        int2 o=int2(x,y); if(all(o==0))continue;
        int2 q=clamp(p+o,int2(0,0),dim-1);
        o=q-p;
        float e=patch(p,q,dim);
        bool better=(e<best-eps)||(abs(e-best)<=eps && length2(o)<length2(bo));
        if(better){second=best;best=e;bo=o;}
        else second=min(second,e);
    }
    if(second==1e9)second=best;
    // Equal/near-equal alternatives deliberately yield low confidence.
    float conf=saturate((second-best)/(max(second,1e-4)));
    Flow[id.xy]=float4(float2(bo)*Downsample,conf,best);
}
