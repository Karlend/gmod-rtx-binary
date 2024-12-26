#include "material_converter.h"
#include <algorithm>
#include <cctype>
#include <platform.h>
#include "utils/interfaces.h"

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

    // Test material system functionality
    IMaterial* testMat = materials->FindMaterial("debug/debugempty", TEXTURE_GROUP_OTHER);
    if (!testMat) {
        Warning("[Material Converter] Failed to find test material\n");
        return false;
    }

    Msg("[Material Converter] Successfully initialized with material system at %p\n", materials);
    return true;
}

bool MaterialConverter::IsProblematicMaterial(const char* materialName) {
    static bool firstCheck = true;
    if (firstCheck) {
        Msg("[Material Converter] First material check: %s\n", materialName ? materialName : "null");
        firstCheck = false;
    }

    if (!materialName) return false;

    // Convert to lowercase for case-insensitive comparison
    std::string lowerName = materialName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    // Check against problematic patterns
    for (const auto& pattern : m_problematicPatterns) {
        if (lowerName.find(pattern) != std::string::npos) {
            Msg("[Material Converter] Found problematic pattern '%s' in material '%s'\n", 
                pattern.c_str(), materialName);
            return true;
        }
    }

    return false;
}

IMaterial* MaterialConverter::ProcessMaterial(IMaterial* material) {
    if (!material) return nullptr;

    const char* materialName = material->GetName();
    if (!materialName) return material;

    // Check if we should process this material
    if (!ShouldProcessMaterial(materialName)) {
        return material;
    }

    // Log the processing
    LogMaterialProcess(materialName, true);

    // Get or create safe version
    return GetSafeMaterial(materialName);
}

bool MaterialConverter::ShouldProcessMaterial(const char* materialName) {
    if (!materialName) return false;

    // Check cache first
    std::unordered_map<std::string, IMaterial*>::iterator it = m_materialCache.find(materialName);
    if (it != m_materialCache.end()) {
        return true;
    }

    return IsProblematicMaterial(materialName);
}

void MaterialConverter::LogMaterialProcess(const char* materialName, bool wasProcessed) {
    static float lastLogTime = 0.0f;
    float currentTime = Plat_FloatTime();

    // Only log every second to avoid spam
    if (currentTime - lastLogTime < 1.0f) return;

    Msg("[Material Converter] %s material: %s\n",
        wasProcessed ? "Processing" : "Skipping",
        materialName ? materialName : "unknown");

    lastLogTime = currentTime;
}

IMaterial* MaterialConverter::GetSafeMaterial(const char* originalName) {
    // Check cache
    std::unordered_map<std::string, IMaterial*>::iterator it = m_materialCache.find(originalName);
    if (it != m_materialCache.end()) {
        return it->second;
    }

    // Create safe material
    KeyValues* kv = new KeyValues("UnlitGeneric");
    kv->SetString("$basetexture", "debug/debugempty");
    kv->SetInt("$translucent", 1);
    kv->SetInt("$vertexalpha", 1);
    kv->SetInt("$vertexcolor", 1);

    char safeName[512];
    snprintf(safeName, sizeof(safeName), "__safe_%s", originalName);

    IMaterial* safeMaterial = materials->CreateMaterial(safeName, kv);
    kv->deleteThis();

    // Cache the result
    if (safeMaterial) {
        m_materialCache[originalName] = safeMaterial;
    }

    return safeMaterial;
}

IMaterial* __fastcall MaterialSystem_FindMaterial_detour(void* thisptr, void* edx, 
    const char* materialName, const char* textureGroupName, bool complain, const char* complainPrefix) {
    
    // Always log the first call to verify the hook is working
    static bool firstCall = true;
    if (firstCall) {
        Msg("[Hook Debug] First call to FindMaterial detour!\n");
        Msg("[Hook Debug] Material: %s\n", materialName ? materialName : "null");
        Msg("[Hook Debug] Group: %s\n", textureGroupName ? textureGroupName : "null");
        firstCall = false;
    }

    // Log every problematic material
    if (materialName && MaterialConverter::Instance().IsProblematicMaterial(materialName)) {
        Msg("[Material Converter] Found problematic material: %s\n", materialName);
    }

    if (!g_original_FindMaterial) {
        Error("[Hook Debug] Original FindMaterial function is null!\n");
        return nullptr;
    }

    return g_original_FindMaterial(thisptr, materialName, textureGroupName, complain);
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

        // Try to call a simple function
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