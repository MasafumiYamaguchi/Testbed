#include "phase.hlsli"
#include "preview_approx.hlsli"
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
    float4 inverse0,inverse1,inverse2,quality,sunSettings,majorantSettings,progressiveSettings,debugSettings,approxSettings;
};
float3 localVector(float3 p) {return float3(dot(inverse0.xyz,p),dot(inverse1.xyz,p),dot(inverse2.xyz,p));}
float3 localPoint(float3 p) {return localVector(p)+float3(inverse0.w,inverse1.w,inverse2.w);}
float evaluateDensity(float3 p) {
    if(quality.w==0)return densityAt(p);
    return constrainCache(p,cachedDensity.SampleLevel(cacheLinear,(p-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz),0));
}
float shadowOpticalDepth(float3 origin,out uint evaluations) {
    evaluations=0;
    if(sunSettings.x!=0){
        if(any(origin<=envelopeMin.xyz)||any(origin>=envelopeMax.xyz))return 0;
        return max(0,sunTau.SampleLevel(sunLinear,(origin-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz),0));
    }
    float3 direction=localVector(lightAlbedo.xyz);float entry=0,exit=3.402823466e38; // Sun visibility is independent of camera clipping.
    if(!intersectBox(origin,direction,envelopeMin.xyz,envelopeMax.xyz,entry,exit))return 0;
    float dt=(exit-entry)/quality.y,tau=0;
    evaluations=(uint)quality.y;
    for(uint i=0;i<(uint)quality.y;++i)tau+=evaluateDensity(origin+direction*(entry+(i+0.5)*dt))*forwardExtinction.w*dt;
    return tau;
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
    uint viewEvaluations=0,totalEvaluations=0,skips=0;float viewTau=0,sunSum=0,weight=0;bool invalid=false;
    bool hit=intersectBox(localOrigin,localDirection,envelopeMin.xyz,envelopeMax.xyz,entry,exit);
    if(hit) {
        float dt=(exit-entry)/quality.x;
        for(uint i=0;i<(uint)quality.x;) {
            float3 q=localOrigin+localDirection*(entry+(i+0.5)*dt);
            if(majorantSettings.w!=0&&debugSettings.x!=11){
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
                    if(debugSettings.x>0)++skips;
                    i+=advance;continue;
                }
            }
            float density=evaluateDensity(q);
            if(debugSettings.x>0){++viewEvaluations;++totalEvaluations;viewTau+=density*forwardExtinction.w*dt;invalid=invalid||!isfinite(density)||density<0;
                if(debugSettings.x==11&&majorantSettings.w!=0){uint3 brick=min(uint3(max(0,(q-envelopeMin.xyz)/(envelopeMax.xyz-envelopeMin.xyz)*majorantSettings.xyz))/8,(uint3(majorantSettings.xyz)+7)/8-1);invalid=invalid||density>brickMax.Load(int4(brick,0))+2e-6;}
            }
            if(density>0) {
                uint evaluations=0;float tauSun=shadowOpticalDepth(q,evaluations);float shadow=exp(-tauSun);
                if(debugSettings.x>0){totalEvaluations+=evaluations;sunSum+=shadow*density*dt;weight+=density*dt;}
                float3 source=irradianceFar.xyz*previewApproxSource(tauSun,lightAlbedo.w,upUnused.w,dot(lightAlbedo.xyz,direction),1,approxSettings.x!=0&&debugSettings.x!=4,approxSettings.y);
                integrateSegment(L,T,density*forwardExtinction.w,dt,source);
            }
            ++i;
        }
    }
    uint mode=(uint)debugSettings.x;
    if(mode>0){
        float scalar=0;float3 slicePoint=lerp(envelopeMin.xyz,envelopeMax.xyz,float3(uv.x,1-uv.y,.5));
        if(mode==1)scalar=evaluateDensity(slicePoint);
        else if(mode==2)scalar=viewTau;
        else if(mode==3)scalar=weight>0?sunSum/weight:1;
        else if(mode==4)return float4(L,T);
        else if(mode==5)scalar=hit?1:0;
        else if(mode==6||mode==7){if(majorantSettings.w!=0){uint3 brick=min(uint3(float3(uv.x,1-uv.y,.5)*majorantSettings.xyz)/8,(uint3(majorantSettings.xyz)+7)/8-1);scalar=brickMax.Load(int4(brick,0));if(mode==7)scalar=scalar>0?1:0;}}
        else if(mode==8)scalar=viewEvaluations;
        else if(mode==9)scalar=totalEvaluations;
        else if(mode==10)scalar=skips;
        else if(mode==11)return float4(invalid?float3(1,0,1):float3(0,0,0),T);
        return float4(scalar,scalar,scalar,T);
    }
    // A constant background, not atmospheric or multiple scattering.
    return float4(L+T*float3(0.015,0.022,0.035),T);
}
