#include "noise.hlsli"
#include "developed_density.hlsli"
#ifndef DENSITY_SPACE
#define DENSITY_SPACE space2
#endif
cbuffer Cloud : register(b1,DENSITY_SPACE){DevelopedDensityPacket densityPacket;};
#define envelopeMin densityPacket.envelopeMin
#define envelopeMax densityPacket.envelopeMax
float densityAt(float3 p){return developedDensityAt(p,densityPacket);}
