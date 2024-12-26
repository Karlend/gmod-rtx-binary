#pragma once
#include "utils/interfaces.h"
#include <tier0/dbg.h>
#include <materialsystem/imaterialsystem.h>
#include <materialsystem/imaterial.h>
#include <materialsystem/imaterialvar.h>
#include <tier1/KeyValues.h>
#include <detouring/hook.hpp>
#include <unordered_set>
#include <string>

typedef IMaterial* (__thiscall* FindMaterial_t)(void* thisptr, const char* materialName, const char* textureGroupName, bool complain);
extern FindMaterial_t g_original_FindMaterial;

class MaterialConverter {
public:
    static MaterialConverter& Instance();
    static bool VerifyMaterialSystem();

    bool Initialize();
    bool IsBlockedShader(const char* shaderName) const;
    bool IsBlockedMaterial(const char* materialName) const;

private:
    MaterialConverter() = default;
    ~MaterialConverter() = default;

    // Lists of blocked shaders and materials
    const std::unordered_set<std::string> m_blockedShaders = {
        "spritecard",
        "wireframe",
        "wireframe_dx9",
        "sdk_bloom",
        "sdk_bloomadd",
        "sdk_blur",
        "sdk_water",
        "sdk_eye_refract",
        "sdk_shadowbuild",
        "sdk_sprite",
        "sprite",
        "unlittwotexture",
        "vertexlitgeneric",
        "worldtwotextureblend",
        "lightmappedgeneric"
    };

    const std::unordered_set<std::string> m_blockedMaterialPrefixes = {
        "particle/",
        "effects/",
        "sprites/",
        "engine/",
        "debug/",
        "shaders/",
        "pp/",
        "models/effects/",
        "materials/effects/",
        "materialsrc/",
        "materials/overlays/"
    };
};

// Hook function
IMaterial* __fastcall MaterialSystem_FindMaterial_detour(void* thisptr, void* edx, 
    const char* materialName, const char* textureGroupName, bool complain, const char* complainPrefix);