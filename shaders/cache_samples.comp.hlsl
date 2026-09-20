#include "density.hlsli"
#include "cache.hlsli"
Texture3D<float> field : register(t0,space0);
SamplerState linearClamp : register(s0,space0);
RWStructuredBuffer<float4> results : register(u0,space1);
[numthreads(1,1,1)]
void main(uint3 id:SV_DispatchThreadID){
    uint h=noiseHash(id.x+12345);float3 uv=float3(h&1023,(h>>10)&1023,(h>>20)&1023)/1023.0;
    float3 p=lerp(envelopeMin.xyz,envelopeMax.xyz,uv);
    if(id.x==0)p=float3(0,densityPacket.groups[0].settings.x+densityPacket.translations[0].y-0.1,0);
    if(id.x==1&&densityPacket.groups[0].config.w>0)p=densityPacket.groups[0].cutCenters[0].xyz+densityPacket.translations[0].xyz-float3(densityPacket.groups[0].cutRadii[0].x-0.1,0,0);
    if(id.x==2)p=envelopeMin.xyz+0.1;
    if(id.x==3)p=envelopeMax.xyz-0.1;
    if(topLobePacket.mask.x!=0){
        float3 anchor=densityPacket.groups[1].centers[0].xyz;
        if(id.x>=4&&id.x<=7){
            float dy=id.x==4?-0.001:id.x==5?0:id.x==6?0.001:densityPacket.groups[1].settings.y*0.5;
            p=float3(anchor.x,topLobePacket.mask.y+dy,anchor.z);
        }
        if(id.x==8)p=anchor;
        if(id.x==9&&densityPacket.groups[1].config.z>1)p=densityPacket.groups[1].centers[1].xyz;
    }
    if(cloudPacket.settings.x!=0){
        float3 center=cloudPacket.center.xyz;
        float3 direction=cloudPacket.direction.xyz;
        float3 side=float3(-direction.z,0,direction.x);
        float3 anchor=center-direction*(cloudPacket.dimensions.x-cloudPacket.dimensions.y);
        if(id.x>=10&&id.x<=12)p=float3(anchor.x,cloudPacket.settings.z+(float(id.x)-11)*0.001,anchor.z);
        if(id.x==13)p=center;
        float edge=1-cloudPacket.dimensions.w/(2*cloudPacket.dimensions.z);
        if(id.x==14)p=center+direction*(cloudPacket.dimensions.x*edge);
        if(id.x==15)p=center+side*(cloudPacket.dimensions.y*edge);
    }
    // An active sheet uses direct evaluation even when its base cloud still
    // contains one density group. The CPU expects density plus actual XYZ in
    // this branch, not the legacy density/cache/constrained-cache tuple.
    if(densityPacket.settings.x==2||cloudPacket.settings.x!=0||finishStack.settings.x>0){results[id.x]=float4(densityAt(p),p);return;}
    float raw=field.SampleLevel(linearClamp,(p-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz),0);
    results[id.x]=float4(densityAt(p),raw,constrainCache(p,raw),1);
}
