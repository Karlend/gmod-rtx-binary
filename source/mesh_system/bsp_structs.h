#pragma once
#include "materialsystem/imaterial.h"
#include "mathlib/vector.h"

#define MAX_LIGHTMAPS 4

struct msurface_t;
struct mtexinfo_t;
struct mvertex_t;

struct model_t {
    int numsurfaces;
    msurface_t* surfaces;
    int leaffaces;
    unsigned short* leafindices;
};

struct msurface_t {
    int flags;
    int firstedge;
    short numedges;
    short texinfo;
    short dispinfo;
    short surfaceID;
    struct mvertex_t* verts;
    int numverts;
    unsigned short* indexes;
    int numindexes;
    struct mtexinfo_t* texinfo;
    ColorRGBExp32* samples;
    int lightmaptexturenum;
    int lightmapStyles[MAX_LIGHTMAPS];
};

struct mtexinfo_t {
    float textureVecs[2][4];
    float lightmapVecs[2][4];
    int flags;
    IMaterial* material;
};

struct CDispInfo {
    int GetPower() const { return 0; }
    bool GetVert(int index, Vector& pos, Vector& normal, float& alpha) const { return false; }
    bool GetTexCoord(int index, Vector2D& coord) const { return false; }
    bool GetLightmapSample(int index, Vector& color) const { return false; }
};