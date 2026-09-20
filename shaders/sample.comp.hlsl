Texture3D<float> field : register(t0, space0);
SamplerState linearClamp : register(s0, space0);
RWStructuredBuffer<float> results : register(u0, space1);
cbuffer Params : register(b0, space2) { uint3 extent; uint fixture; };
[numthreads(1,1,1)]
void main(uint3 p : SV_DispatchThreadID) {
    // Texel centers, a fractional interior position, and clamp-to-edge cases.
    float3 index = float3(0,0,0);
    if (p.x == 1) index = float3(extent) - 1.0;
    if (p.x == 2) index = (float3(extent) - 1.0) * float3(0.27, 0.43, 0.61);
    if (p.x == 3) index = float3(-0.5,-0.5,-0.5);
    if (p.x == 4) index = float3(extent) - 0.5;
    results[p.x] = field.SampleLevel(linearClamp, (index + 0.5) / float3(extent), 0);
}
