cbuffer Scene : register(b0){uint Width,Height,Frame,Hdr;};
struct V {float4 p:SV_Position;};
V VSMain(uint id:SV_VertexID){V o;o.p=float4(id==0?float2(-1,-1):id==1?float2(-1,3):float2(3,-1),0,1);return o;}
struct Targets{float4 color:SV_Target0;float depth:SV_Target1;float2 motion:SV_Target2;};
float3 eye(float f){return float3(sin(f*.012)*.8+floor(f/240)*.6,1.5,-5);}
float3 center(float f){return float3(sin(f*.035)*1.4,1,1);}
float2 project(float3 p,float3 camera){float3 v=p-camera;float2 ndc=v.xy/max(v.z,.001)/.57735;ndc.x/=float(Width)/Height;return float2((ndc.x+1)*.5,(1-ndc.y)*.5)*float2(Width,Height);}
float sphere(float3 ro,float3 rd,float3 c){float3 oc=ro-c;float b=dot(oc,rd),d=b*b-dot(oc,oc)+1;return d>0?-b-sqrt(d):1e6;}
// Fixed bitmap digits make a high-contrast HUD independent of scene motion.
uint glyph(uint n){const uint bits[10]={31599,9362,29671,29391,23497,31183,31215,29257,31727,31695};return bits[n%10];}
Targets PSMain(V v){
 uint2 pixel=uint2(v.p.xy);float2 ndc=v.p.xy/float2(Width,Height)*2-1;ndc.y=-ndc.y;
 float previous=Frame%240==0?Frame:Frame-1;
 float3 camera=eye(Frame),oldCamera=eye(previous),rd=normalize(float3(ndc.x*(float(Width)/Height)*.57735,ndc.y*.57735,1));
 float plane=rd.y<-.001?-camera.y/rd.y:1e6;float ball=sphere(camera,rd,center(Frame));
 float t=min(plane,ball);bool hit=t<100,object=ball<plane;
 float3 world=camera+rd*t,oldWorld=world;
 float3 color=float3(.08,.12,.2)+max(rd.y,0)*.15;
 if(hit){
  float3 normal=object?normalize(world-center(Frame)):float3(0,1,0);
  float checker=fmod(abs(floor(world.x*4)+floor(world.z*4)),2);
  float light=.2+.8*saturate(dot(normal,normalize(float3(-1,3,-2))));
  color=object?float3(.8,.2,.06)*light:lerp(float3(.08,.1,.12),float3(.65,.7,.75),checker)*light;
  if(object)oldWorld+=center(previous)-center(Frame);
  if(Hdr!=0)color*=object?10:2;
 }
 Targets o;o.color=float4(color,1);o.motion=hit?project(oldWorld,oldCamera)-v.p.xy:0;
 float z=hit?(world.z-camera.z):100;o.depth=hit?saturate(100/99.9-.1*100/(99.9*z)):1;
 // Far bright patch, thin diagonals, and frame counter cover highlight,
 // disocclusion and small text behavior without deriving depth from color.
 if(pixel.x>Width*3/4&&pixel.y<Height/4){o.color=float4(Hdr!=0?float3(12,8,3):float3(.9,.7,.3),1);o.motion=0;o.depth=1;}
 if(pixel.y<42&&pixel.x<140){uint2 p=pixel/3;uint digit=p.x/4;uint value=Frame;for(uint i=0;i<digit;i++)value/=10;bool ink=(p.x%4)<3&&p.y>=2&&p.y<7&&((glyph(value)>>(p.x%4+(p.y-2)*3))&1);o.color=float4(ink?1:.01,ink?1:.01,ink?1:.01,1);o.motion=0;o.depth=1;}
 return o;
}
