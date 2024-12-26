#pragma once
#include "utils/interfaces.h"
#include <tier0/dbg.h>
#include <materialsystem/imaterialsystem.h>
#include <materialsystem/imaterial.h>
#include <materialsystem/imaterialvar.h>
#include <tier1/KeyValues.h>
#include <detouring/hook.hpp>
#include <unordered_set>
#include <unordered_map>
#include <string>

typedef IMaterial* (__thiscall* FindMaterial_t)(void* thisptr, const char* materialName, const char* textureGroupName, bool complain);
extern FindMaterial_t g_original_FindMaterial;

class MaterialConverter {
public:
    static bool VerifyMaterialSystem();
    static MaterialConverter& Instance();

    // Core functions
    bool Initialize();
    bool IsProblematicMaterial(const char* materialName);
    IMaterial* ProcessMaterial(IMaterial* material);

private:
    MaterialConverter() = default;
    ~MaterialConverter() = default;

    // Problematic material patterns
    std::unordered_set<std::string> m_problematicPatterns = {
        "fire",
        "explosion",
        "flame",
        "burn",
        "particle",
        "effects",
        "smoke",
        "spark",
        "beam",
        "sprite",
        "trail",
        "engine",
        "occlusionproxy"
    };

    // Material processing
    bool ShouldProcessMaterial(const char* materialName);
    void LogMaterialProcess(const char* materialName, bool wasProcessed);
    IMaterial* GetSafeMaterial(const char* originalName);

    // Cache processed materials
    std::unordered_map<std::string, IMaterial*> m_materialCache;
};

// Hook function
IMaterial* __fastcall MaterialSystem_FindMaterial_detour(void* thisptr, void* edx, 
    const char* materialName, const char* textureGroupName, bool complain, const char* complainPrefix);