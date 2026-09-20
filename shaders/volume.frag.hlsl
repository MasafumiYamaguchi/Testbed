#include "phase.hlsli"
#define DENSITY_SPACE space3
#include "density.hlsli"
#include "optics.hlsli"
#include "cache.hlsli"
Texture3D<float> cachedDensity : register(t0,space2);
SamplerState cacheLinear : register(s0,space2);
Texture3D<float> sunTau : register(t1,space2);
SamplerState sunLinear : register(s1,space2);
Texture3D<float> brickMax : register(t2,space2);
SamplerState brickPoint : register(s2,space2);
cbuffer View : register(b0,space3) {
    float4 eyeNear, rightTan, upUnused, forwardExtinction, lightAlbedo, irradianceFar;
    float4 inverse0,inverse1,inverse2,quality,sunSettings,majorantSettings,progressiveSettings;
};
float3 localVector(float3 p) {return float3(dot(inverse0.xyz,p),dot(inverse1.xyz,p),dot(inverse2.xyz,p));}
float3 localPoint(float3 p) {return localVector(p)+float3(inverse0.w,inverse1.w,inverse2.w);}
float evaluateDensity(float3 p) {
    if(quality.w==0)return densityAt(p);
    return constrainCache(p,cachedDensity.SampleLevel(cacheLinear,(p-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz),0));
}
float shadowTransmittance(float3 origin) {
    if(sunSettings.x!=0){
        if(any(origin<=envelopeMin.xyz)||any(origin>=envelopeMax.xyz))return 1;
        return exp(-max(0,sunTau.SampleLevel(sunLinear,(origin-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz),0)));
    }
    float3 direction=localVector(lightAlbedo.xyz);float entry=0,exit=irradianceFar.w;
    if(!intersectBox(origin,direction,envelopeMin.xyz,envelopeMax.xyz,entry,exit))return 1;
    float dt=(exit-entry)/quality.y,tau=0;
    for(uint i=0;i<(uint)quality.y;++i)tau+=evaluateDensity(origin+direction*(entry+(i+0.5)*dt))*forwardExtinction.w*dt;
    return exp(-tau);
}
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target0 {
    if(progressiveSettings.y!=0){uint pixel=uint(position.y)*uint(1/progressiveSettings.z)+uint(position.x);
        float2 jitter=float2(phaseRandom(pixel,uint(progressiveSettings.x),0,uint2(42,0)),phaseRandom(pixel,uint(progressiveSettings.x),1,uint2(42,0)))-.5;
        uv+=jitter*progressiveSettings.zw;
    }
    float2 p=float2(uv.x*2-1,1-uv.y*2);
    float3 direction=normalize(forwardExtinction.xyz+rightTan.xyz*(p.x*quality.z*rightTan.w)+upUnused.xyz*(p.y*rightTan.w));
    float3 localOrigin=localPoint(eyeNear.xyz),localDirection=localVector(direction);
    float entry=eyeNear.w,exit=irradianceFar.w;float3 L=0;float T=1;
    if(intersectBox(localOrigin,localDirection,envelopeMin.xyz,envelopeMax.xyz,entry,exit)) {
        float dt=(exit-entry)/quality.x;
        for(uint i=0;i<(uint)quality.x;) {
            float3 q=localOrigin+localDirection*(entry+(i+0.5)*dt);
            if(majorantSettings.w!=0){
                float3 edges=(q-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz)*majorantSettings.xyz;
                uint3 brick=min(uint3(max(0,edges))/8,(uint3(majorantSettings.xyz)+7)/8-1);
                if(brickMax.Load(int4(brick,0))==0){
                    float untilExit=1e30;
                    for(uint axis=0;axis<3;++axis)if(localDirection[axis]!=0){
                        float edge=localDirection[axis]>0?min((brick[axis]+1)*8,majorantSettings[axis]):brick[axis]*8;
                        float face=lerp(envelopeMin[axis],envelopeMax[axis],edge/majorantSettings[axis]);
                        untilExit=min(untilExit,max(0,(face-q[axis])/localDirection[axis]));
                    }
                    // Keep the original global midpoint lattice. Leave a full
                    // sample before the face to tolerate boundary roundoff.
                    uint advance=(uint)clamp(floor(untilExit/dt)-1,1,quality.x-i);
                    i+=advance;continue;
                }
            }
            float density=evaluateDensity(q);
            if(density>0) {
                float3 source=irradianceFar.xyz*(lightAlbedo.w*phaseHG(dot(lightAlbedo.xyz,direction),upUnused.w))*shadowTransmittance(q);
                integrateSegment(L,T,density*forwardExtinction.w,dt,source);
            }
            ++i;
        }
    }
    // A constant background, not atmospheric or multiple scattering.
    return float4(L+T*float3(0.015,0.022,0.035),T);
}
