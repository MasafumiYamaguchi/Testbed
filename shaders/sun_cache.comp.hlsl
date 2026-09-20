#include "density.hlsli"
#include "cache.hlsli"
#include "optics.hlsli"
Texture3D<float> density : register(t0,space0);
SamplerState linearClamp : register(s0,space0);
RWTexture3D<float> opticalDepth : register(u0,space1);
cbuffer SunCache : register(b0,space2) {float4 localSunSigma;float4 dimensionsSteps;float4 farUnused;};
[numthreads(4,4,4)]
void main(uint3 id:SV_DispatchThreadID){
    if(any(id>=uint3(dimensionsSteps.xyz)))return;
    float3 p=lerp(envelopeMin.xyz,envelopeMax.xyz,(float3(id)+.5)/dimensionsSteps.xyz);
    float entry=0,exit=3.402823466e38,tau=0;
    if(intersectBox(p,localSunSigma.xyz,envelopeMin.xyz,envelopeMax.xyz,entry,exit)){
        float dt=(exit-entry)/dimensionsSteps.w;
        for(uint i=0;i<(uint)dimensionsSteps.w;++i){float3 q=p+localSunSigma.xyz*(entry+(i+.5)*dt);
            tau+=constrainCache(q,density.SampleLevel(linearClamp,(q-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz),0))*localSunSigma.w*dt;}
    }
    opticalDepth[id]=tau;
}
