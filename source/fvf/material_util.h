#pragma once
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "tier0/dbg.h"

/*
Material utility functions
Responsibilities:
- Determine if material should use fixed function
- Parse material parameters
- Handle different shader types
*/
namespace MaterialUtil {
    // Checks if material should use fixed function pipeline
    bool ShouldUseFixedFunction(IMaterial* material);
    // Material type checks
    bool IsWorldMaterial(IMaterial* material);
    bool IsModelMaterial(IMaterial* material);
}