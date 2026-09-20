bool opticalInputsValid(float sigma,float distance,float3 source){
    return isfinite(sigma)&&isfinite(distance)&&all(isfinite(source))&&sigma>=0&&distance>=0&&all(source>=0);
}
float segmentOpacity(float tau) {
    // Avoid cancellation in 1-exp(-tau) near vacuum.
    return tau<1e-3 ? tau*(1-tau*(0.5-tau/6)) : 1-exp(-tau);
}
void integrateSegment(inout float3 L,inout float T,float sigma,float distance,float3 source) {
    float opacity=segmentOpacity(sigma*distance);
    L+=T*source*opacity;T*=exp(-sigma*distance);
}
void integrateConstantSource(inout float3 L,inout float T,float sigma,float distance,float3 j){
    float tau=sigma*distance;
    float integral=tau<1e-3?distance*(1-tau*(0.5-tau/6)):segmentOpacity(tau)/sigma;
    L+=T*j*integral;T*=exp(-tau);
}
bool intersectSlab(float origin,float direction,float lo,float hi,inout float entry,inout float exit) {
    if(direction==0)return origin>=lo&&origin<=hi;
    float a=(lo-origin)/direction,b=(hi-origin)/direction;
    entry=max(entry,min(a,b));exit=min(exit,max(a,b));
    return exit>entry;
}
bool intersectBox(float3 origin,float3 direction,float3 lo,float3 hi,inout float entry,inout float exit) {
    // Explicit scalar slabs avoid dynamic vector indexing/local arrays in DXIL.
    if(!intersectSlab(origin.x,direction.x,lo.x,hi.x,entry,exit))return false;
    if(!intersectSlab(origin.y,direction.y,lo.y,hi.y,entry,exit))return false;
    if(!intersectSlab(origin.z,direction.z,lo.z,hi.z,entry,exit))return false;
    return exit>entry;
}
