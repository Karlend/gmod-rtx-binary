#include "material_util.h"

namespace MaterialUtil {
    bool ShouldUseFixedFunction(IMaterial* material) {
        if (!material) return false;
        
        const char* shaderName = material->GetShaderName();
        const char* materialName = material->GetName();
        
        if (!shaderName) return false;

        static float lastLogTime = 0.0f;
        float currentTime = Plat_FloatTime();
        
        if (currentTime - lastLogTime > 1.0f) {
            Msg("[Material Util] Checking material: %s (shader: %s)\n", 
                materialName ? materialName : "null",
                shaderName);
            lastLogTime = currentTime;
        }

        // Start with basic shader checks - add logging for testing
        bool shouldUse = (strstr(shaderName, "VertexLitGeneric") ||
                         strstr(shaderName, "LightmappedGeneric"));
                         
        if (shouldUse) {
            Msg("[Material Util] Will use fixed function for %s\n", materialName);
        }

        return shouldUse;
    }

    bool IsWorldMaterial(IMaterial* material) {
        if (!material) return false;
        const char* name = material->GetName();
        return name && strstr(name, "world");
    }

    bool IsModelMaterial(IMaterial* material) {
        if (!material) return false;
        const char* name = material->GetName();
        return name && strstr(name, "model");
    }
}