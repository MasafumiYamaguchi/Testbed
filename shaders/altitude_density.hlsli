#ifndef WHITE_ALTITUDE_DENSITY_HLSLI
#define WHITE_ALTITUDE_DENSITY_HLSLI
// params = { local base, local height, enabled knot count (0 means disabled),
//             maximum knot scale }. The CPU validates a maximum of eight knots.
float altitudeDensityScale(float localY,float4 params,float4 knots[8]) {
    uint count=(uint)params.z;
    if(count==0)return 1;
    float t=saturate((localY-params.x)/params.y);
    [unroll]for(uint i=1;i<8;++i) {
        if(i<count&&t<knots[i].x) {
            float u=(t-knots[i-1].x)/(knots[i].x-knots[i-1].x);
            return lerp(knots[i-1].y,knots[i].y,u);
        }
    }
    return knots[count-1].y;
}
#endif
