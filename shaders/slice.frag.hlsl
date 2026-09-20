Texture3D<float> field : register(t0, space2);
SamplerState linearClamp : register(s0, space2);
cbuffer View : register(b0, space3) { float slice; uint axis; uint fixture; float unused; };
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    float2 p = saturate(uv);
    float3 q = axis == 0 ? float3(slice,p.x,1-p.y) : (axis == 1 ? float3(p.x,slice,1-p.y) : float3(p.x,1-p.y,slice));
    float value = field.SampleLevel(linearClamp, q, 0);
    // A diagnostic color ramp; scalar values are untouched in the 3D texture.
    float3 cold = float3(0.035,0.09,0.19);
    float3 warm = float3(0.25,0.86,0.86);
    return float4(lerp(cold,warm,saturate(value)),1);
}
