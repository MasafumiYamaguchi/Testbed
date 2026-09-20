Texture3D<float> density : register(t0,space0);
SamplerState unusedSampler : register(s0,space0);
RWTexture3D<float> maxima : register(u0,space1);
cbuffer Dimensions : register(b0,space2){uint3 extent;uint unused;};
[numthreads(4,4,4)]
void main(uint3 brick:SV_DispatchThreadID){
    if(any(brick>=(extent+7)/8))return;float m=0;
    for(int z=-1;z<=8;++z)for(int y=-1;y<=8;++y)for(int x=-1;x<=8;++x){int3 p=clamp(int3(brick*8)+int3(x,y,z),0,int3(extent)-1);m=max(m,density.Load(int4(p,0)));}
    maxima[brick]=m;
}
