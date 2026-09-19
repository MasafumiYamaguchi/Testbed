float constrainCache(float3 p,float value) {
    if(any(p<=envelopeMin.xyz)||any(p>=envelopeMax.xyz)||(config.y!=0&&p.y<=settings.x))return 0;
    for(uint i=0;i<(uint)config.w;++i)if(ellipsoid(p,cutCenters[i].xyz,cutRadii[i].xyz)<=0)return 0;
    return value;
}
