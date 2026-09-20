#ifndef WHITE_TOP_LOBES_HLSLI
#define WHITE_TOP_LOBES_HLSLI
#include "developed_density.hlsli"
struct TopLobePacket {DevelopedDensityPacket fields;float4 mask;};
float topLobeDensityAt(float3 objectLocal,TopLobePacket packet){
    if(packet.mask.x==0)return developedDensityAt(objectLocal,packet.fields);
    // The lower fixed region retains the exact OFF legacy arithmetic.
    if(objectLocal.y<=packet.mask.y)return developedLegacyDensityAt(objectLocal-packet.fields.translations[0].xyz,packet.fields.groups[0]);
    return developedDensityAt(objectLocal,packet.fields);
}
#endif
