#include "optics.hlsli"
RWStructuredBuffer<float4> results : register(u0,space1);
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    const float sigmas[8]={0,0.01,0.1,1,10,0.0000001,0.25,2};
    const float distances[8]={100,20,15,2,10,100,8,0};
    const uint steps[8]={1,7,32,64,128,256,16,5};
    float3 L=0;float T=1;
    for(uint i=0;i<steps[id.x];++i)integrateSegment(L,T,sigmas[id.x],distances[id.x]/steps[id.x],float3(0.5,0.5,0.5));
    results[id.x]=float4(T,L);
}
