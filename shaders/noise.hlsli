uint noiseHash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float latticeNoise(int3 p,uint seed){uint3 u=asuint(p);return (noiseHash(u.x*0x8da6b343u^u.y*0xd8163841u^u.z*0xcb1ab31fu^seed)&0xffffffu)/16777215.0;}
float valueNoise(float3 p,uint seed){
    int3 i=(int3)floor(p);float3 f=p-floor(p);f=f*f*f*(f*(f*6-15)+10);
    return saturate(lerp(lerp(lerp(latticeNoise(i,seed),latticeNoise(i+int3(1,0,0),seed),f.x),lerp(latticeNoise(i+int3(0,1,0),seed),latticeNoise(i+int3(1,1,0),seed),f.x),f.y),
        lerp(lerp(latticeNoise(i+int3(0,0,1),seed),latticeNoise(i+int3(1,0,1),seed),f.x),lerp(latticeNoise(i+int3(0,1,1),seed),latticeNoise(i+int3(1,1,1),seed),f.x),f.y),f.z));
}
float detailNoise(float3 p,uint seed){return (2*valueNoise(p,seed)+valueNoise(p*2,seed^0x9e3779b9u))/3;}
float3 domainDisplacement(float3 p,uint seed,float maximumMetres){return (float3(valueNoise(p,seed),valueNoise(p,seed^0xa511e9b3u),valueNoise(p,seed^0x63d83595u))*2-1)*(maximumMetres*0.5773502691896258);}
