RWTexture3D<float> field : register(u0, space1);
cbuffer Params : register(b0, space2) { uint3 extent; uint fixture; };
[numthreads(4,4,4)]
void main(uint3 p : SV_DispatchThreadID) {
    if (any(p >= extent)) return;
    float ramp = (float(p.x) + 2.0 * float(p.y) + 4.0 * float(p.z)) / 256.0;
    field[p] = fixture == 0 ? ramp : (all(p == extent / 2) ? 1.0 : 0.0);
}
