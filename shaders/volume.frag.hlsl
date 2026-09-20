#include "phase.hlsli"
#define DENSITY_SPACE space3
#include "density.hlsli"
#include "optics.hlsli"
#include "cache.hlsli"
Texture3D<float> cachedDensity : register(t0,space2);
SamplerState cacheLinear : register(s0,space2);
cbuffer View : register(b0,space3) {
    float4 eyeNear, rightTan, upUnused, forwardExtinction, lightAlbedo, irradianceFar;
    float4 inverse0,inverse1,inverse2,quality;
};
float3 localVector(float3 p) {return float3(dot(inverse0.xyz,p),dot(inverse1.xyz,p),dot(inverse2.xyz,p));}
float3 localPoint(float3 p) {return localVector(p)+float3(inverse0.w,inverse1.w,inverse2.w);}
float evaluateDensity(float3 p) {
    if(quality.w==0)return densityAt(p);
    return constrainCache(p,cachedDensity.SampleLevel(cacheLinear,(p-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz),0));
}
float shadowTransmittance(float3 origin) {
    float3 direction=localVector(lightAlbedo.xyz);float entry=0,exit=irradianceFar.w;
    if(!intersectBox(origin,direction,envelopeMin.xyz,envelopeMax.xyz,entry,exit))return 1;
    float dt=(exit-entry)/quality.y,tau=0;
    for(uint i=0;i<(uint)quality.y;++i)tau+=evaluateDensity(origin+direction*(entry+(i+0.5)*dt))*forwardExtinction.w*dt;
    return exp(-tau);
}
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target0 {
    float2 p=float2(uv.x*2-1,1-uv.y*2);
    float3 direction=normalize(forwardExtinction.xyz+rightTan.xyz*(p.x*quality.z*rightTan.w)+upUnused.xyz*(p.y*rightTan.w));
    float3 localOrigin=localPoint(eyeNear.xyz),localDirection=localVector(direction);
    float entry=eyeNear.w,exit=irradianceFar.w;float3 L=0;float T=1;
    if(intersectBox(localOrigin,localDirection,envelopeMin.xyz,envelopeMax.xyz,entry,exit)) {
        float dt=(exit-entry)/quality.x;
        for(uint i=0;i<(uint)quality.x;++i) {
            float3 q=localOrigin+localDirection*(entry+(i+0.5)*dt);
            float density=evaluateDensity(q);
            if(density>0) {
                float3 source=irradianceFar.xyz*(lightAlbedo.w*phaseHG(dot(lightAlbedo.xyz,direction),upUnused.w))*shadowTransmittance(q);
                integrateSegment(L,T,density*forwardExtinction.w,dt,source);
            }
        }
    }
    // A constant background, not atmospheric or multiple scattering.
    return float4(L+T*float3(0.015,0.022,0.035),T);
}
