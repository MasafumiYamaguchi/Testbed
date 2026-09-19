#include "optics.hlsli"
RWStructuredBuffer<float4> results : register(u0,space1);
[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(id.x>=14){
        float3 L=0;float T=1;
        if(id.x==14)integrateConstantSource(L,T,0,3,float3(2,2,2));
        if(id.x==15)integrateConstantSource(L,T,1e-8,5,float3(1e-9,1e-9,1e-9));
        if(id.x==16){integrateConstantSource(L,T,.1,2,float3(.3,.3,.3));integrateConstantSource(L,T,.5,1,float3(.2,.2,.2));}
        if(id.x==17){integrateConstantSource(L,T,.5,1,float3(.2,.2,.2));integrateConstantSource(L,T,.1,2,float3(.3,.3,.3));}
        if(id.x==18)integrateConstantSource(L,T,.025,40,float3(.05,.05,.05));
        if(id.x==19)for(uint i=0;i<64;++i)integrateConstantSource(L,T,.125,.125,float3(.3,.3,.3));
        if(id.x>=20&&id.x<=22){uint steps=16u<<(id.x-20);float ds=10.0/steps;for(uint i=0;i<steps;++i){float x=(i+.5)*ds;integrateSegment(L,T,.02+.003*x*x,ds,float3(.6,.6,.6));}}
        if(id.x==23){results[id.x]=float4(opticalInputsValid(-1,1,1),opticalInputsValid(1,-1,1),opticalInputsValid(asfloat(0x7fc00000u),1,1),opticalInputsValid(1,1,1));return;}
        results[id.x]=float4(T,L);return;
    }
    if(id.x>=8) {
        float3 origin=float3(-3,0,0),direction=float3(1,0,0);float entry=0,exit=100;
        if(id.x==9)origin=0;
        if(id.x==10)origin.y=2;
        if(id.x==11)origin.y=1;
        if(id.x==12){origin=float3(3,0,0);direction=float3(-1,0,0);}
        if(id.x==13){origin=float3(3,3,3);direction=normalize(float3(-1,-1,-1));}
        bool hit=intersectBox(origin,direction,float3(-1,-1,-1),float3(1,1,1),entry,exit);
        results[id.x]=float4(hit,hit?entry:0,hit?exit:0,1);return;
    }
    const float sigmas[8]={0,0.01,0.1,1,10,0.0000001,0.25,2};
    const float distances[8]={100,20,15,2,10,100,8,0};
    const uint steps[8]={1,7,32,64,128,256,16,5};
    float3 L=0;float T=1;
    for(uint i=0;i<steps[id.x];++i)integrateSegment(L,T,sigmas[id.x],distances[id.x]/steps[id.x],float3(0.5,0.5,0.5));
    results[id.x]=float4(T,L);
}
