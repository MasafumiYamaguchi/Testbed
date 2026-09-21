#ifndef WHITE_DEVELOPED_DENSITY_HLSLI
#define WHITE_DEVELOPED_DENSITY_HLSLI
#include "altitude_density.hlsli"
// Include after noise.hlsli. These are pure field functions; the consuming
// preview/bake shader owns the uniform binding. No per-pixel mutable state.
// Byte-for-byte mirror of GpuDensityParams (912 bytes) and GpuDevelopedParams
// (1904 bytes). Every member/array element occupies a complete 16-byte register.
struct DevelopedDensityGroup {
    float4 centers[8];
    float4 radii[8];
    float4 cutCenters[8];
    float4 cutRadii[8];
    float4 envelopeMin;
    float4 envelopeMax;
    float4 settings;
    float4 config;
    uint4 cellKeys[8];
    float4 noiseOrigin;
    float4 noiseBands;
    float4 noiseWarp;
    uint4 noiseSeeds;
    float4 altitudeDensityParams;
    float4 altitudeDensityKnots[8];
};
struct DevelopedDensityPacket {
    DevelopedDensityGroup groups[2];
    float4 translations[2];
    float4 envelopeMin;
    float4 envelopeMax;
    float4 settings; // group count, fusion metres, overlap, maximum density
};

float developedSmooth01(float x){x=saturate(x);return x*x*(3-2*x);}
float developedEllipsoid(float3 p,float3 center,float3 radii){return (length((p-center)/radii)-1)*min(radii.x,min(radii.y,radii.z));}
float developedSmoothUnion(float a,float b,float k){
    if(k==0)return min(a,b);
    float h=max(k-abs(a-b),0)/k;return min(a,b)-h*h*k*0.25;
}
float developedCoverage(float distance){return 1-developedSmooth01((distance+2)/2);}

// Preserve legacy evaluation order for exactly one development, including its
// own finite envelope, hard clipping, overlap, noise and altitude profile.
float developedLegacyDensityAt(float3 p,DevelopedDensityGroup group){
    uint count=(uint)group.config.z,cutCount=(uint)group.config.w;
    if(count==0||any(p<=group.envelopeMin.xyz)||any(p>=group.envelopeMax.xyz))return 0;
    if(group.config.y!=0&&p.y<=group.settings.x)return 0;
    float merged=0,sum=0;
    for(uint i=0;i<count;++i){
        float3 q=p;
        if(group.noiseWarp.y>0)q+=domainDisplacement((p-group.noiseOrigin.xyz)*group.noiseWarp.x,group.cellKeys[i].x,group.noiseWarp.y);
        float distance=developedEllipsoid(q,group.centers[i].xyz,group.radii[i].xyz);
        merged=i==0?distance:developedSmoothUnion(merged,distance,group.settings.w);sum+=developedCoverage(distance);
    }
    float3 n=p-group.noiseOrigin.xyz;
    if(group.noiseBands.w>0)merged+=group.noiseBands.w*detailNoise(n*group.noiseBands.z,group.noiseSeeds.x^0x6c8e9cf5u);
    float value=group.settings.z*developedCoverage(merged)*(1+group.config.x*max(0,sum-1));
    if(group.noiseBands.y>0)value*=1-group.noiseBands.y*detailNoise(n*group.noiseBands.x,group.noiseSeeds.x);
    if(group.config.y!=0&&group.settings.y>0)value*=developedSmooth01((p.y-group.settings.x)/group.settings.y);
    for(uint c=0;c<cutCount;++c){
        float distance=developedEllipsoid(p,group.cutCenters[c].xyz,group.cutRadii[c].xyz);
        if(distance<=0)return 0;
        if(group.cutRadii[c].w>0)value*=developedSmooth01(distance/group.cutRadii[c].w);
    }
    value*=altitudeDensityScale(p.y,group.altitudeDensityParams,group.altitudeDensityKnots);
    return clamp(value,0,group.settings.z*(1+group.config.x*(count-1))*group.altitudeDensityParams.w);
}

struct DevelopedShapeSample {float distance;float coefficient;float density;};
DevelopedShapeSample developedShapeAt(float3 p,DevelopedDensityGroup group){
    DevelopedShapeSample result=(DevelopedShapeSample)0;
    uint count=(uint)group.config.z,cutCount=(uint)group.config.w;
    if(count==0||group.settings.z==0||(group.config.y!=0&&p.y<=group.settings.x))return result;
    // Do not clip individual envelopes in a multi-development evaluation: the
    // geometric bridge can extend beyond them. Global support is conservative.
    for(uint c=0;c<cutCount;++c)if(developedEllipsoid(p,group.cutCenters[c].xyz,group.cutRadii[c].xyz)<=0)return result;
    float merged=0,sum=0;
    for(uint i=0;i<count;++i){
        float3 q=p;
        if(group.noiseWarp.y>0)q+=domainDisplacement((p-group.noiseOrigin.xyz)*group.noiseWarp.x,group.cellKeys[i].x,group.noiseWarp.y);
        float distance=developedEllipsoid(q,group.centers[i].xyz,group.radii[i].xyz);
        merged=i==0?distance:developedSmoothUnion(merged,distance,group.settings.w);sum+=developedCoverage(distance);
    }
    float3 n=p-group.noiseOrigin.xyz;
    if(group.noiseBands.w>0)merged+=group.noiseBands.w*detailNoise(n*group.noiseBands.z,group.noiseSeeds.x^0x6c8e9cf5u);
    float factor=group.settings.z*(1+group.config.x*max(0,sum-1));
    if(group.noiseBands.y>0)factor*=1-group.noiseBands.y*detailNoise(n*group.noiseBands.x,group.noiseSeeds.x);
    if(group.config.y!=0&&group.settings.y>0)factor*=developedSmooth01((p.y-group.settings.x)/group.settings.y);
    for(uint c=0;c<cutCount;++c)if(group.cutRadii[c].w>0)factor*=developedSmooth01(developedEllipsoid(p,group.cutCenters[c].xyz,group.cutRadii[c].xyz)/group.cutRadii[c].w);
    factor*=altitudeDensityScale(p.y,group.altitudeDensityParams,group.altitudeDensityKnots);
    result.distance=merged;result.coefficient=factor;result.density=developedCoverage(merged)*factor;return result;
}

float developedDensityAt(float3 objectLocal,DevelopedDensityPacket packet){
    uint count=(uint)packet.settings.x;
    if(count==0||any(objectLocal<=packet.envelopeMin.xyz)||any(objectLocal>=packet.envelopeMax.xyz))return 0;
    if(count==1)return developedLegacyDensityAt(objectLocal-packet.translations[0].xyz,packet.groups[0]);
    if(count!=2)return asfloat(0x7fc00000u); // Corrupt packet is not transparent.
    DevelopedShapeSample a=developedShapeAt(objectLocal-packet.translations[0].xyz,packet.groups[0]);
    DevelopedShapeSample b=developedShapeAt(objectLocal-packet.translations[1].xyz,packet.groups[1]);
    float fused=developedCoverage(developedSmoothUnion(a.distance,b.distance,packet.settings.y));
    float separate=max(developedCoverage(a.distance),developedCoverage(b.distance));
    float bridge=max(0,fused-separate)*min(a.coefficient,b.coefficient);
    float density=max(a.density,b.density)+bridge+packet.settings.z*min(a.density,b.density);
    return clamp(density,0,packet.settings.w);
}

float developedConstrainCache(float3 objectLocal,float value,DevelopedDensityPacket packet){
    uint count=(uint)packet.settings.x;
    if(count==0||any(objectLocal<=packet.envelopeMin.xyz)||any(objectLocal>=packet.envelopeMax.xyz))return 0;
    // A single aggregate R32F cache cannot reconstruct independently masked
    // overlapping developments. The caller must force two-group Direct mode
    // and disable sun-cache / empty-skip consumers. Do not invent an OR mask.
    if(count!=1)return asfloat(0x7fc00000u);
    DevelopedDensityGroup group=packet.groups[0];
    float3 p=objectLocal-packet.translations[0].xyz;
    if(any(p<=group.envelopeMin.xyz)||any(p>=group.envelopeMax.xyz)||(group.config.y!=0&&p.y<=group.settings.x))return 0;
    for(uint i=0;i<(uint)group.config.w;++i)if(developedEllipsoid(p,group.cutCenters[i].xyz,group.cutRadii[i].xyz)<=0)return 0;
    return value;
}
#endif
