#include "material_converter.h"
#include <algorithm>
#include <cctype>

FindMaterial_t g_original_FindMaterial = nullptr;

MaterialConverter& MaterialConverter::Instance() {
    static MaterialConverter instance;
    return instance;
}

bool MaterialConverter::Initialize() {
    if (!materials) {
        Warning("[Material Converter] Failed to initialize - no material system\n");
        return false;
    }

    Msg("[Material Converter] Successfully initialized\n");
    return true;
}

bool MaterialConverter::IsBlockedShader(const char* shaderName) const {
    if (!shaderName) return false;

    std::string lowerName = shaderName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    return m_blockedShaders.find(lowerName) != m_blockedShaders.end();
}

bool MaterialConverter::IsBlockedMaterial(const char* materialName) const {
    if (!materialName) return false;

    std::string lowerName = materialName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    for (const auto& prefix : m_blockedMaterialPrefixes) {
        if (lowerName.find(prefix) == 0) {
            return true;
        }
    }

    return false;
}

bool MaterialConverter::VerifyMaterialSystem() {
    if (!materials) {
        Msg("[Material Debug] materials interface is null\n");
        return false;
    }

    try {
        // Try to get vtable
        void** vtable = *reinterpret_cast<void***>(materials);
        if (!vtable) {
            Msg("[Material Debug] Failed to get vtable\n");
            return false;
        }

        IMaterial* test = materials->FindMaterial("debug/debugempty", TEXTURE_GROUP_OTHER);
        if (!test) {
            Msg("[Material Debug] Failed to find test material\n");
            return false;
        }

        Msg("[Material Debug] Material system verified working\n");
        return true;
    }
    catch (...) {
        Msg("[Material Debug] Exception while verifying material system\n");
        return false;
    }
}

IMaterial* __fastcall MaterialSystem_FindMaterial_detour(void* thisptr, void* edx, 
    const char* materialName, const char* textureGroupName, bool complain, const char* complainPrefix) {
    
    try {
        static IMaterial* fallbackMaterial = nullptr;
        static bool firstCall = true;

        // Initialize fallback material once
        if (!fallbackMaterial) {
            fallbackMaterial = g_original_FindMaterial(thisptr, "debug/debugempty", TEXTURE_GROUP_OTHER, false);
            if (!fallbackMaterial) {
                Warning("[Material Converter] Failed to get fallback material\n");
                return nullptr;
            }
            fallbackMaterial->IncrementReferenceCount();
        }

        if (firstCall) {
            Msg("[Hook Debug] First call to FindMaterial detour!\n");
            Msg("[Hook Debug] Material: %s\n", materialName ? materialName : "null");
            Msg("[Hook Debug] Group: %s\n", textureGroupName ? textureGroupName : "null");
            firstCall = false;
        }

        // Early checks
        if (!materialName) {
            return fallbackMaterial;
        }

        // Check if it's a problematic material name pattern
        if (MaterialConverter::Instance().IsBlockedMaterial(materialName)) {
            Msg("[Material Converter] Blocking problematic material: %s\n", materialName);
            return fallbackMaterial;
        }

        // Get the original material
        IMaterial* material = nullptr;
        try {
            material = g_original_FindMaterial(thisptr, materialName, textureGroupName, false);  // Set complain to false
        }
        catch (...) {
            Warning("[Material Converter] Exception getting material %s\n", materialName);
            return fallbackMaterial;
        }

        if (!material || material->IsErrorMaterial()) {
            return fallbackMaterial;
        }

        // Check shader type
        try {
            const char* shaderName = material->GetShaderName();
            if (shaderName && MaterialConverter::Instance().IsBlockedShader(shaderName)) {
                Msg("[Material Converter] Blocking problematic shader: %s for material %s\n", 
                    shaderName, materialName);
                return fallbackMaterial;
            }
        }
        catch (...) {
            Warning("[Material Converter] Exception checking shader for %s\n", materialName);
            return fallbackMaterial;
        }

        return material;
    }
    catch (...) {
        Warning("[Material Converter] Top-level exception in FindMaterial detour\n");
        return g_original_FindMaterial(thisptr, "debug/debugempty", TEXTURE_GROUP_OTHER, false);
    }
}