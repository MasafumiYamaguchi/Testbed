#include "density.hlsli"
RWTexture3D<float> field : register(u0, space1);
cbuffer Params : register(b0, space2) { uint3 extent; uint fixture; };
[numthreads(4,4,4)]
void main(uint3 p : SV_DispatchThreadID) {
    if (any(p >= extent)) return;
    float ramp = (float(p.x) + 2.0 * float(p.y) + 4.0 * float(p.z)) / 256.0;
    if(fixture==0)field[p]=ramp;
    else if(fixture==1)field[p]=all(p==extent/2)?1.0:0.0;
    else field[p]=densityAt(lerp(envelopeMin.xyz,envelopeMax.xyz,(float3(p)+0.5)/float3(extent)));
}
