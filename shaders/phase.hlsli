uint phaseMix(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float phaseRandom(uint pixel,uint sample,uint dimension,uint2 seed){
    uint key=phaseMix(seed.x)^phaseMix(seed.y^0x9e3779b9u);
    uint bits=phaseMix(key^phaseMix(pixel+0x68bc21ebu)^phaseMix(sample+0x02e5be93u)^phaseMix(dimension+0x967a889bu));
    return (bits>>8)*(1.0/16777216.0);
}
float phaseHG(float cosine,float g){
    cosine=clamp(cosine,-1,1);
    float d=g>=0?(1-g)*(1-g)+2*g*(1-cosine):(1+g)*(1+g)-2*g*(1+cosine);
    return (1-g*g)/(12.566370614359172*d*sqrt(d));
}
float sampleHGCosine(float g,float u){float a=2*u-1,den=1+g*a;return clamp((a*(1+g*g)+.5*g*(a*a+3)+.5*g*g*g*(a*a-1))/(den*den),-1,1);}
float3 sampleHG(float3 incoming,float g,float2 u){
    float cosine=sampleHGCosine(g,u.x),sine=sqrt(max(0,1-cosine*cosine)),phi=6.283185307179586*u.y;
    float3 helper=abs(incoming.z)<.999?float3(0,0,1):float3(1,0,0);
    float3 tangent=normalize(cross(helper,incoming)),bitangent=cross(incoming,tangent);
    return incoming*cosine+sine*(tangent*cos(phi)+bitangent*sin(phi));
}
