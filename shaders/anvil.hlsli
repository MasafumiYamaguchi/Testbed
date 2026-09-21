#ifndef WHITE_ANVIL_HLSLI
#define WHITE_ANVIL_HLSLI
#include "top_lobes.hlsli"
struct AnvilPacket {
    TopLobePacket cloud;
    float4 center,direction,dimensions,settings,envelopeMin,envelopeMax;
};
float anvilDensityAt(float3 p,AnvilPacket packet){
    float original=topLobeDensityAt(p,packet.cloud);
    if(packet.settings.x==0||p.y<=packet.settings.z)return original;
    float3 q=p-packet.center.xyz;
    q-=packet.direction.xyz*(packet.direction.w*q.y);
    float3 side=float3(-packet.direction.z,0,packet.direction.x);
    float u=dot(q,packet.direction.xyz)/packet.dimensions.x;
    float v=dot(q,side)/packet.dimensions.y;
    float w=q.y/packet.dimensions.z;
    float distance=(length(float3(u,v,w))-1)*packet.dimensions.z;
    DevelopedDensityGroup group=packet.cloud.fields.groups[0];
    float3 local=p-packet.cloud.fields.translations[0].xyz;
    float3 n=local-group.noiseOrigin.xyz;
    if(group.noiseBands.w>0)distance+=group.noiseBands.w*detailNoise(n*group.noiseBands.z,group.noiseSeeds.x^0x6c8e9cf5u);
    float addition=group.settings.z*packet.settings.y*(1-developedSmooth01((distance+packet.dimensions.w)/packet.dimensions.w));
    if(addition==0)return original;
    if(group.noiseBands.y>0)addition*=1-group.noiseBands.y*detailNoise(n*group.noiseBands.x,group.noiseSeeds.x);
    if(group.config.y!=0){if(local.y<=group.settings.x)return original;if(group.settings.y>0)addition*=developedSmooth01((local.y-group.settings.x)/group.settings.y);}
    for(uint c=0;c<(uint)group.config.w;++c){float d=developedEllipsoid(local,group.cutCenters[c].xyz,group.cutRadii[c].xyz);if(d<=0)return original;if(group.cutRadii[c].w>0)addition*=developedSmooth01(d/group.cutRadii[c].w);}
    addition*=altitudeDensityScale(local.y,group.altitudeDensityParams,group.altitudeDensityKnots);
    return clamp(max(original,addition),0,packet.settings.w);
}
#endif
