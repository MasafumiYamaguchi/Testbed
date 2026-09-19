float segmentOpacity(float tau) {
    // Avoid cancellation in 1-exp(-tau) near vacuum.
    return tau<1e-3 ? tau*(1-tau*(0.5-tau/6)) : 1-exp(-tau);
}
void integrateSegment(inout float3 L,inout float T,float sigma,float distance,float3 source) {
    float opacity=segmentOpacity(sigma*distance);
    L+=T*source*opacity;T*=1-opacity;
}
bool intersectBox(float3 origin,float3 direction,float3 lo,float3 hi,inout float entry,inout float exit) {
    for(uint axis=0;axis<3;++axis) {
        if(direction[axis]==0) {if(origin[axis]<lo[axis]||origin[axis]>hi[axis])return false;}
        else {
            float a=(lo[axis]-origin[axis])/direction[axis],b=(hi[axis]-origin[axis])/direction[axis];
            entry=max(entry,min(a,b));exit=min(exit,max(a,b));
            if(exit<=entry)return false;
        }
    }
    return exit>entry;
}
