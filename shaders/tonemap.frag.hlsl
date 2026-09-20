Texture2D<float4> hdr : register(t0,space2);
SamplerState pointClamp : register(s0,space2);
cbuffer Display : register(b0,space3) {float exposureEV;float3 unused;};
float srgb(float x) {return x<=0.0031308?12.92*x:1.055*pow(x,1.0/2.4)-0.055;}
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target0 {
    float3 L=max(hdr.SampleLevel(pointClamp,uv,0).rgb,0)*exp2(exposureEV);
    L=L/(1+L);
    return float4(srgb(L.r),srgb(L.g),srgb(L.b),1);
}
