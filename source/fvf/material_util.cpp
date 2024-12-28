#include "material_util.h"

namespace MaterialUtil {
    bool MaterialUtil::ShouldUseFixedFunction(IMaterial* material) {
        if (!material) {
            Msg("[Material Util] Null material\n");
            return false;
        }
        
        const char* shaderName = material->GetShaderName();
        const char* materialName = material->GetName();
        
        if (!shaderName) {
            Msg("[Material Util] Null shader name\n");
            return false;
        }

        Msg("[Material Util] Checking material: %s (shader: %s)\n", 
            materialName ? materialName : "null",
            shaderName);

        // For testing, try to intercept everything
        return true; // Force fixed function for all materials
    }
}