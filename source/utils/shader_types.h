// source/mesh_system/shader_types.h
#pragma once

// Shader blend modes
#define SHADER_BLEND_ZERO                     0
#define SHADER_BLEND_ONE                      1
#define SHADER_BLEND_DST_COLOR               2
#define SHADER_BLEND_ONE_MINUS_DST_COLOR     3
#define SHADER_BLEND_SRC_ALPHA               4
#define SHADER_BLEND_ONE_MINUS_SRC_ALPHA     5
#define SHADER_BLEND_DST_ALPHA               6
#define SHADER_BLEND_ONE_MINUS_DST_ALPHA     7
#define SHADER_BLEND_SRC_ALPHA_SATURATE      8
#define SHADER_BLEND_SRC_COLOR               9
#define SHADER_BLEND_ONE_MINUS_SRC_COLOR     10

// Stencil operations
#define SHADER_STENCILOP_KEEP                1
#define SHADER_STENCILOP_ZERO                2
#define SHADER_STENCILOP_REPLACE             3
#define SHADER_STENCILOP_INCRSAT             4
#define SHADER_STENCILOP_DECRSAT             5
#define SHADER_STENCILOP_INVERT              6
#define SHADER_STENCILOP_INCR                7
#define SHADER_STENCILOP_DECR                8

// Stencil comparison functions
#define SHADER_STENCILFUNC_NEVER             1
#define SHADER_STENCILFUNC_LESS              2
#define SHADER_STENCILFUNC_EQUAL             3
#define SHADER_STENCILFUNC_LESSEQUAL         4
#define SHADER_STENCILFUNC_GREATER           5
#define SHADER_STENCILFUNC_NOTEQUAL          6
#define SHADER_STENCILFUNC_GREATEREQUAL      7
#define SHADER_STENCILFUNC_ALWAYS            8

// Stencil state structure
struct ShaderStencilState_t {
    bool m_bEnable;
    int m_nReferenceValue;    // 0-255
    int m_nTestMask;          // 0-255
    int m_nWriteMask;         // 0-255
    int m_CompareFunc;        // SHADER_STENCILFUNC_*
    int m_PassOp;            // SHADER_STENCILOP_*
    int m_FailOp;            // SHADER_STENCILOP_*
    int m_ZFailOp;           // SHADER_STENCILOP_*

    ShaderStencilState_t() {
        m_bEnable = false;
        m_nReferenceValue = 0;
        m_nTestMask = 0xFF;
        m_nWriteMask = 0xFF;
        m_CompareFunc = SHADER_STENCILFUNC_ALWAYS;
        m_PassOp = SHADER_STENCILOP_KEEP;
        m_FailOp = SHADER_STENCILOP_KEEP;
        m_ZFailOp = SHADER_STENCILOP_KEEP;
    }
};

// Material override state structure
struct MaterialOverrideState_t {
    bool m_bOverrideDepthWrite;
    bool m_bOverrideAlphaWrite;
    bool m_bEnableDepthWrite;
    bool m_bEnableAlphaWrite;

    MaterialOverrideState_t() {
        m_bOverrideDepthWrite = false;
        m_bOverrideAlphaWrite = false;
        m_bEnableDepthWrite = true;
        m_bEnableAlphaWrite = true;
    }
};

// Render state helper class
class RenderStateHelper {
public:
    static void SetDefaultStates(IMatRenderContext* pRenderContext) {
        if (!pRenderContext) return;

        // Reset stencil state
        ShaderStencilState_t defaultStencil;
        pRenderContext->SetStencilState(defaultStencil);

        // Reset material override
        MaterialOverrideState_t defaultMaterial;
        pRenderContext->SetMaterialOverrideState(defaultMaterial);

        // Reset blend mode
        pRenderContext->OverrideBlend(false, 0, 0);
    }

    static void SetupTranslucentState(IMatRenderContext* pRenderContext) {
        if (!pRenderContext) return;

        // Setup stencil state for translucent rendering
        ShaderStencilState_t stencilState;
        stencilState.m_bEnable = true;
        stencilState.m_nReferenceValue = 1;
        stencilState.m_nTestMask = 0xFF;
        stencilState.m_nWriteMask = 0xFF;
        stencilState.m_CompareFunc = SHADER_STENCILFUNC_ALWAYS;
        stencilState.m_PassOp = SHADER_STENCILOP_KEEP;
        stencilState.m_FailOp = SHADER_STENCILOP_KEEP;
        stencilState.m_ZFailOp = SHADER_STENCILOP_KEEP;
        pRenderContext->SetStencilState(stencilState);

        // Setup material state for translucent rendering
        MaterialOverrideState_t materialState;
        materialState.m_bOverrideDepthWrite = true;
        materialState.m_bOverrideAlphaWrite = true;
        materialState.m_bEnableDepthWrite = false;
        materialState.m_bEnableAlphaWrite = true;
        pRenderContext->SetMaterialOverrideState(materialState);

        // Setup blend mode for translucent rendering
        pRenderContext->OverrideBlend(true, 
            SHADER_BLEND_SRC_ALPHA, 
            SHADER_BLEND_ONE_MINUS_SRC_ALPHA);
    }
};