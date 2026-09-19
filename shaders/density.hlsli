#include "noise.hlsli"
// Shared pointwise field for preview, bake and future export.
// Approximate ellipsoid implicit distance; never use as a sphere-tracing step.
#ifndef DENSITY_SPACE
#define DENSITY_SPACE space2
#endif
cbuffer Cloud : register(b1, DENSITY_SPACE) {
    float4 centers[8]; float4 radii[8]; float4 cutCenters[8]; float4 cutRadii[8];
    float4 envelopeMin; float4 envelopeMax; float4 settings; float4 config;
    uint4 cellKeys[8];float4 noiseOrigin,noiseBands,noiseWarp;uint4 noiseSeeds;
};
float smooth01(float x) {x=saturate(x);return x*x*(3-2*x);}
float ellipsoid(float3 p,float3 c,float3 r) {return (length((p-c)/r)-1)*min(r.x,min(r.y,r.z));}
float smoothUnion(float a,float b,float k) {
    if(k==0)return min(a,b);
    float h=max(k-abs(a-b),0)/k;return min(a,b)-h*h*k*0.25;
}
float coverage(float d) {return 1-smooth01((d+2)/2);}
float densityAt(float3 p) {
    uint count=(uint)config.z,cutCount=(uint)config.w;
    if(count==0||any(p<=envelopeMin.xyz)||any(p>=envelopeMax.xyz))return 0;
    if(config.y!=0&&p.y<=settings.x)return 0;
    float merged=0,sum=0;
    for(uint i=0;i<count;++i) {
        float3 q=p;
        if(noiseWarp.y>0)q+=domainDisplacement((p-noiseOrigin.xyz)*noiseWarp.x,cellKeys[i].x,noiseWarp.y);
        float d=ellipsoid(q,centers[i].xyz,radii[i].xyz);
        merged=i==0?d:smoothUnion(merged,d,settings.w);sum+=coverage(d);
    }
    float3 n=p-noiseOrigin.xyz;
    if(noiseBands.w>0)merged+=noiseBands.w*detailNoise(n*noiseBands.z,noiseSeeds.x^0x6c8e9cf5u);
    float value=settings.z*coverage(merged)*(1+config.x*max(0,sum-1));
    if(noiseBands.y>0)value*=1-noiseBands.y*detailNoise(n*noiseBands.x,noiseSeeds.x);
    if(config.y!=0&&settings.y>0)value*=smooth01((p.y-settings.x)/settings.y);
    for(uint c=0;c<cutCount;++c) {
        float d=ellipsoid(p,cutCenters[c].xyz,cutRadii[c].xyz);
        if(d<=0)return 0;
        if(cutRadii[c].w>0)value*=smooth01(d/cutRadii[c].w);
    }
    return clamp(value,0,settings.z*(1+config.x*(count-1)));
}
