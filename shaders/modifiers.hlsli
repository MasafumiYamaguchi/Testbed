#ifndef WHITE_MODIFIERS_HLSLI
#define WHITE_MODIFIERS_HLSLI
#include "anvil.hlsli"
struct FinishLayer {float4 center,radii,operation,target;};
struct FinishStack {FinishLayer layers[4];float4 settings;};
struct FinishedPacket {AnvilPacket field;FinishStack finish;};
float finishedDensityAt(float3 p,FinishedPacket packet){
    float2 density=1,detail=1,hard=1;
    for(uint i=0;i<(uint)packet.finish.settings.x;++i){
        FinishLayer layer=packet.finish.layers[i];if(layer.operation.y==0||layer.operation.z==0)continue;
        float distance=developedEllipsoid(p,layer.center.xyz,layer.radii.xyz);
        float weight=1-developedSmooth01(distance/layer.center.w),amount=layer.operation.z*weight;
        uint target=(uint)layer.target.x,kind=(uint)layer.operation.x;
        for(uint group=0;group<2;++group)if((target&(1u<<group))!=0){
            // The hard flag is resolved from the source double, so a partial
            // strength rounding to float 1 cannot change layer semantics.
            if(kind==0){if(layer.target.y!=0)hard[group]*=1-weight;else density[group]=max(0,density[group]-amount);}
            else if(kind==1)density[group]*=1+(layer.operation.w-1)*amount;
            else detail[group]*=1-amount;
        }
    }
    for(uint group=0;group<2;++group){
        density[group]*=hard[group];
        packet.field.cloud.fields.groups[group].settings.z*=density[group];
        packet.field.cloud.fields.groups[group].noiseBands.y*=detail[group];
        packet.field.cloud.fields.groups[group].noiseBands.w*=detail[group];
        packet.field.cloud.fields.groups[group].noiseWarp.y*=detail[group];
    }
    packet.field.cloud.fields.settings.w*=packet.finish.settings.y;
    packet.field.settings.w*=packet.finish.settings.y;
    // Exactly one expanded density kernel; no dual no-detail/detail evaluation.
    return anvilDensityAt(p,packet.field);
}
#endif
