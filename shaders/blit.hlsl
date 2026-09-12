Texture2D<float4> Src:register(t0);SamplerState Samp:register(s0);
struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};
V VSMain(uint id:SV_VertexID){V o;float2 p=id==0?float2(-1,-1):id==1?float2(-1,3):float2(3,-1);o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,1-(p.y+1)*.5);return o;}
float4 PSMain(V i):SV_Target{return Src.SampleLevel(Samp,i.uv,0);}
