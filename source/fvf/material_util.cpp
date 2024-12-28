#include "material_util.h"
#include "ff_logging.h"

namespace MaterialUtil {
    bool MaterialUtil::ShouldUseFixedFunction(IMaterial* material) {
        if (!material) return false;
        
        const char* shaderName = material->GetShaderName();
        const char* materialName = material->GetName();
        
        if (!shaderName || !materialName) return false;

        // List of material name patterns to skip
        static const char* skip_patterns[] = {
            "vgui*",        // VGUI elements
            "__*",           // Special materials
            "debug*",        // Debug materials
            "tools/*",       // Tool materials
            "engine/*",      // Engine materials
            "console/*",     // Console materials
            "effects/*",     // Effect materials
            "sprites/*",     // Sprite materials
            "particle/*",    // Particle materials
            "*_rt",          // Render targets
            "dev/*",         // Development textures
        };

        // Simple wildcard check function
        auto MatchesWildcard = [](const char* str, const char* pattern) {
            if (!str || !pattern) return false;
            
            // Handle front wildcard
            if (pattern[0] == '*') {
                size_t len = strlen(pattern) - 1;
                size_t strLen = strlen(str);
                return strLen >= len && 
                    strcmp(str + (strLen - len), pattern + 1) == 0;
            }
            // Handle back wildcard
            else if (pattern[strlen(pattern) - 1] == '*') {
                size_t len = strlen(pattern) - 1;
                return strncmp(str, pattern, len) == 0;
            }
            // Handle middle wildcard
            else if (strstr(pattern, "*")) {
                const char* wildcard = strstr(pattern, "*");
                size_t prefixLen = wildcard - pattern;
                const char* suffix = wildcard + 1;
                return strncmp(str, pattern, prefixLen) == 0 && 
                    strstr(str + prefixLen, suffix) != nullptr;
            }
            // Exact match
            else {
                return strcmp(str, pattern) == 0;
            }
        };

        // Check against skip patterns
        for (const char* pattern : skip_patterns) {
            if (MatchesWildcard(materialName, pattern)) {
                static float lastLogTime = 0.0f;
                float currentTime = Plat_FloatTime();
                if (currentTime - lastLogTime > 1.0f) {
                    FF_LOG("Skipping material (matched pattern %s): %s", 
                        pattern, materialName);
                    lastLogTime = currentTime;
                }
                return false;
            }
        }

        // Process world and model materials
        bool isWorld = strstr(shaderName, "LightmappedGeneric") != nullptr;
        bool isModel = strstr(shaderName, "VertexLitGeneric") != nullptr ||
                    strstr(materialName, "models/") != nullptr;

        if (isWorld || isModel) {
            FF_LOG("Processing %s material: %s", isWorld ? "world" : "model", materialName);
            return true;
        }

        static float lastLogTime = 0.0f;
        float currentTime = Plat_FloatTime();
        if (currentTime - lastLogTime > 1.0f) {
            FF_LOG("Material check:");
            FF_LOG("  Name: %s", materialName);
            FF_LOG("  Shader: %s", shaderName);
            FF_LOG("  Is Model: %s", isModel ? "Yes" : "No");
            if (isModel) {
                // Log model-specific material vars
                IMaterialVar* boneCount = material->FindVar("$numbones", nullptr);
                IMaterialVar* modelTexture = material->FindVar("$basetexture", nullptr);
                FF_LOG("  Bones: %d", boneCount ? boneCount->GetIntValue() : 0);
                FF_LOG("  Has Texture: %s", modelTexture ? "Yes" : "No");
            }
            lastLogTime = currentTime;
        }

        return true; // For testing, process all materials
    }

    bool IsModelMaterial(IMaterial* material) {
        if (!material) return false;
        
        const char* shaderName = material->GetShaderName();
        const char* materialName = material->GetName();
        
        if (!shaderName || !materialName) return false;

        // Check both shader name and material path
        bool isModel = (strstr(shaderName, "VertexLitGeneric") != nullptr) ||
                      (strstr(shaderName, "Model") != nullptr) ||
                      (strstr(materialName, "models/") != nullptr);

        static float lastLogTime = 0.0f;
        float currentTime = Plat_FloatTime();
        if (currentTime - lastLogTime > 1.0f) {
            FF_LOG("Model material check:");
            FF_LOG("  Name: %s", materialName);
            FF_LOG("  Shader: %s", shaderName);
            FF_LOG("  Is Model: %s", isModel ? "Yes" : "No");
            if (isModel) {
                // Log model-specific material vars
                IMaterialVar* boneCount = material->FindVar("$numbones", nullptr);
                IMaterialVar* modelTexture = material->FindVar("$basetexture", nullptr);
                FF_LOG("  Bones: %d", boneCount ? boneCount->GetIntValue() : 0);
                FF_LOG("  Has Texture: %s", modelTexture ? "Yes" : "No");
            }
            lastLogTime = currentTime;
        }

        return isModel;
    }
}