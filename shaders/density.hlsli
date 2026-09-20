#include "noise.hlsli"
#include "top_lobes.hlsli"
#ifndef DENSITY_SPACE
#define DENSITY_SPACE space2
#endif
cbuffer Cloud : register(b1,DENSITY_SPACE){TopLobePacket topLobePacket;};
#define densityPacket topLobePacket.fields
#define envelopeMin densityPacket.envelopeMin
#define envelopeMax densityPacket.envelopeMax
float densityAt(float3 p){return topLobeDensityAt(p,topLobePacket);}
