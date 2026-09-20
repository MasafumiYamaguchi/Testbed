#ifndef WHITE_PREVIEW_APPROX_HLSLI
#define WHITE_PREVIEW_APPROX_HLSLI
// Include phase.hlsli before this file. Physical inputs/settings are validated
// on the CPU. Output is source j/sigma_t, not emitted radiance or transmittance.
float previewCollisionWeight(float tau) {
    // Stable 1-exp(-tau); matches the CPU expm1 branch to float precision.
    return tau<1e-3?tau*(1-tau*(.5-tau/6)):1-exp(-tau);
}
float previewApproxSource(float tau,float albedo,float g,float cosine,
    float sunIrradiance,bool enabled,float strength) {
    if(sunIrradiance==0||albedo==0||isinf(tau))return 0;
    float source=sunIrradiance*albedo*phaseHG(cosine,g)*exp(-tau);
    if(enabled&&strength>0&&tau>0) {
        float collisionWeight=previewCollisionWeight(tau),weight=albedo;
        [unroll]for(uint octave=0;octave<2;++octave) {
            tau*=.5;g*=.5;weight*=albedo*strength;
            source+=sunIrradiance*collisionWeight*weight*phaseHG(cosine,g)*exp(-tau);
        }
    }
    return source;
}
#endif
