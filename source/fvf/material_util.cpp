#include "material_util.h"
#include "ff_logging.h"

namespace MaterialUtil {
    bool MaterialUtil::ShouldUseFixedFunction(IMaterial* material) {
        if (!material) {
            return false;
        }
        
        const char* shaderName = material->GetShaderName();
        const char* materialName = material->GetName();
        
        if (!shaderName || !materialName) {
            return false;
        }

        // Only process specific shaders
        static const char* supported_shaders[] = {
            "LightmappedGeneric",
            "VertexLitGeneric",
            "UnlitGeneric",
            "WorldVertexTransition",
        };

        bool shouldUse = false;
        for (const char* shader : supported_shaders) {
            if (strstr(shaderName, shader)) {
                shouldUse = true;
                break;
            }
        }

        // Skip certain materials
        if (strstr(materialName, "debug") ||
            strstr(materialName, "dev/") ||
            strstr(materialName, "engine") ||
            strstr(materialName, "console") ||
            strstr(materialName, "vgui")) {
            shouldUse = false;
        }

        static float lastLogTime = 0.0f;
        float currentTime = Plat_FloatTime();
        if (currentTime - lastLogTime > 1.0f) {
            FF_LOG("Material check: %s (shader: %s) - %s", 
                materialName,
                shaderName,
                shouldUse ? "Using FF" : "Using original");
            lastLogTime = currentTime;
        }

        return shouldUse;
    }
}