#include "noise.hlsli"
#include "modifiers.hlsli"
#ifndef DENSITY_SPACE
#define DENSITY_SPACE space2
#endif
cbuffer Cloud : register(b1,DENSITY_SPACE){FinishedPacket finishedPacket;};
#define cloudPacket finishedPacket.field
#define finishStack finishedPacket.finish
#define topLobePacket cloudPacket.cloud
#define densityPacket topLobePacket.fields
#define envelopeMin cloudPacket.envelopeMin
#define envelopeMax cloudPacket.envelopeMax
float densityAt(float3 p){return finishedDensityAt(p,finishedPacket);}
