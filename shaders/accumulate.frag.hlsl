Texture2D<float4> current : register(t0,space2);
SamplerState pointCurrent : register(s0,space2);
Texture2D<float4> previous : register(t1,space2);
SamplerState pointPrevious : register(s1,space2);
cbuffer Accumulation : register(b0,space3){uint previousSamples;uint3 unused;};
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target0 {
    float4 value=current.SampleLevel(pointCurrent,uv,0);
    if(previousSamples==0)return value;
    float4 mean=previous.SampleLevel(pointPrevious,uv,0);
    return mean+(value-mean)/(previousSamples+1);
}
