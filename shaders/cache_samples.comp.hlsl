#include "density.hlsli"
#include "cache.hlsli"
Texture3D<float> field : register(t0,space0);
SamplerState linearClamp : register(s0,space0);
RWStructuredBuffer<float4> results : register(u0,space1);
[numthreads(1,1,1)]
void main(uint3 id:SV_DispatchThreadID){
    uint h=noiseHash(id.x+12345);float3 uv=float3(h&1023,(h>>10)&1023,(h>>20)&1023)/1023.0;
    float3 p=lerp(envelopeMin.xyz,envelopeMax.xyz,uv);
    if(id.x==0)p=float3(0,settings.x-0.1,0);
    if(id.x==1&&config.w>0)p=cutCenters[0].xyz-float3(cutRadii[0].x-0.1,0,0);
    if(id.x==2)p=envelopeMin.xyz+0.1;
    if(id.x==3)p=envelopeMax.xyz-0.1;
    float raw=field.SampleLevel(linearClamp,(p-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz),0);
    results[id.x]=float4(densityAt(p),raw,constrainCache(p,raw),1);
}
