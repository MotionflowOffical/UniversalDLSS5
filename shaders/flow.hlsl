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
float patch(int2 p,int2 q,int2 dim,int stride,bool reverse){
    float e=0;
    [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++){
        int2 a=clamp(p+int2(x,y)*stride,int2(0,0),dim-1);
        int2 b=clamp(q+int2(x,y)*stride,int2(0,0),dim-1);
        e+=reverse?abs(Prev.Load(int3(a,0))-Cur.Load(int3(b,0))):abs(Cur.Load(int3(a,0))-Prev.Load(int3(b,0)));
    }
    return e/9.0;
}
float length2(int2 v){float2 f=float2(v);return dot(f,f);}
int2 search(int2 p,int2 dim,bool reverse,out float confidence,out float error){
    int2 bestOffset=0;float best=0,second=0;
    // Three to five bounded scales expand capture range. The radius control
    // selects capture tiers without a large quadratic fine-level search.
    int coarsest=Radius>=8?4:Radius>=4?3:2;
    [loop]for(int level=coarsest;level>=0;--level){
        int step=1<<level;int radius=level==coarsest?min((int)Radius,2):1;
        int2 seed=bestOffset;best=patch(p,p+seed,dim,step,reverse);second=1e9;
        for(int y=-radius;y<=radius;y++)for(int x=-radius;x<=radius;x++){
            if(x==0&&y==0)continue;
            int2 candidate=seed+int2(x,y)*step;
            int2 q=p+candidate;
            if(any(q<0)||any(q>=dim))continue;
            float e=patch(p,q,dim,step,reverse);
            bool better=e<best-1e-6||(abs(e-best)<=1e-6&&length2(candidate)<length2(bestOffset));
            if(better){second=best;best=e;bestOffset=candidate;}else second=min(second,e);
        }
    }
    confidence=saturate((second-best)/max(second,1e-4));error=best;
    return bestOffset;
}
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
    uint lw=(Width+Downsample-1)/Downsample,lh=(Height+Downsample-1)/Downsample;
    if(id.x>=lw||id.y>=lh)return;
    if(!HasHistory){Flow[id.xy]=float4(0,0,0,1);return;}
    int2 dim=int2(lw,lh),p=int2(id.xy);
    float conf,error;int2 bo=search(p,dim,false,conf,error);
    float reverseConfidence,reverseError;
    int2 back=search(clamp(p+bo,0,dim-1),dim,true,reverseConfidence,reverseError);
    float consistency=length(float2(bo+back));
    // Occlusion, textureless patches and contradictory correspondences must
    // never become confident motion supplied to the temporal network.
    conf*=saturate(1.0-consistency/1.5)*saturate(1.0-error*8.0);
    if(conf<ConfidenceThreshold)bo=0;
    Flow[id.xy]=float4(float2(bo)*Downsample,conf,error);
}
