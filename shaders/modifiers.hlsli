#ifndef WHITE_MODIFIERS_HLSLI
#define WHITE_MODIFIERS_HLSLI
#include "anvil.hlsli"
struct FinishLayer {float4 center,radii,operation,target;};
struct FinishStack {FinishLayer layers[4];float4 settings;};
struct FinishedPacket {AnvilPacket field;FinishStack finish;};
float finishedDensityAt(float3 p,FinishedPacket packet){
    float2 density=1,detail=1;uint hard=0;
    for(uint i=0;i<(uint)packet.finish.settings.x;++i){
        FinishLayer layer=packet.finish.layers[i];if(layer.operation.y==0||layer.operation.z==0)continue;
        float distance=developedEllipsoid(p,layer.center.xyz,layer.radii.xyz);
        float amount=layer.operation.z*(1-developedSmooth01(distance/layer.center.w));
        uint target=(uint)layer.target.x,kind=(uint)layer.operation.x;
        for(uint group=0;group<2;++group)if((target&(1u<<group))!=0){
            if(kind==0){density[group]=max(0,density[group]-amount);if(layer.target.y!=0&&layer.operation.z==1&&distance<=0)hard|=1u<<group;}
            else if(kind==1)density[group]*=1+(layer.operation.w-1)*amount;
            else detail[group]*=1-amount;
        }
    }
    for(uint group=0;group<2;++group){
        if((hard&(1u<<group))!=0)density[group]=0;
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
