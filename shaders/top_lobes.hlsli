#ifndef WHITE_TOP_LOBES_HLSLI
#define WHITE_TOP_LOBES_HLSLI
#include "developed_density.hlsli"
struct TopLobePacket {DevelopedDensityPacket fields;float4 mask;};
float topLobeDensityAt(float3 objectLocal,TopLobePacket packet){
    // Select the original single-group path below the protected boundary, then
    // invoke the heavy density kernel exactly once. Multiple call sites caused
    // the inlined noise/shape kernel to explode during WARP driver compilation.
    // The group's own envelope and legacy arithmetic remain unchanged.
    if(packet.mask.x!=0&&objectLocal.y<=packet.mask.y)packet.fields.settings.x=1;
    return developedDensityAt(objectLocal,packet.fields);
}
#endif
