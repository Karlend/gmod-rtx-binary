// fixed_function_state.cpp
#include "fixed_function_state.h"
#include <tier0/dbg.h>

#define FF_LOG(fmt, ...) Msg("[FF State] " fmt "\n", ##__VA_ARGS__)
#define FF_WARN(fmt, ...) Warning("[FF State] " fmt "\n", ##__VA_ARGS__)

FixedFunctionState::~FixedFunctionState() {
    if (m_state.vertexShader) m_state.vertexShader->Release();
    if (m_state.pixelShader) m_state.pixelShader->Release();
}

void FixedFunctionState::Store(IDirect3DDevice9* device) {
    if (!device) return;

    // Store shaders
    device->GetVertexShader(&m_state.vertexShader);
    device->GetPixelShader(&m_state.pixelShader);
    device->GetFVF(&m_state.fvf);

    // Store matrices
    device->GetTransform(D3DTS_WORLD, &m_state.world);
    device->GetTransform(D3DTS_VIEW, &m_state.view);
    device->GetTransform(D3DTS_PROJECTION, &m_state.projection);

    // Store render states
    device->GetRenderState(D3DRS_LIGHTING, &m_state.lighting);
    device->GetRenderState(D3DRS_AMBIENT, &m_state.ambient);
    device->GetRenderState(D3DRS_COLORVERTEX, &m_state.colorVertex);
    device->GetRenderState(D3DRS_CULLMODE, &m_state.cullMode);
    device->GetRenderState(D3DRS_ZENABLE, &m_state.zEnable);
    device->GetRenderState(D3DRS_ALPHABLENDENABLE, &m_state.alphaBlendEnable);
    device->GetRenderState(D3DRS_SRCBLEND, &m_state.srcBlend);
    device->GetRenderState(D3DRS_DESTBLEND, &m_state.destBlend);

    // Store texture stages (store 8 stages)
    m_state.textureStages.clear();
    for (DWORD i = 0; i < 8; i++) {
        StoreTextureStage(device, i);
    }

    m_isStored = true;
    FF_LOG("Device state stored");
}

void FixedFunctionState::Restore(IDirect3DDevice9* device) {
    if (!device || !m_isStored) return;

    // Restore shaders
    device->SetVertexShader(m_state.vertexShader);
    device->SetPixelShader(m_state.pixelShader);
    device->SetFVF(m_state.fvf);

    // Restore matrices
    device->SetTransform(D3DTS_WORLD, &m_state.world);
    device->SetTransform(D3DTS_VIEW, &m_state.view);
    device->SetTransform(D3DTS_PROJECTION, &m_state.projection);

    // Restore render states
    device->SetRenderState(D3DRS_LIGHTING, m_state.lighting);
    device->SetRenderState(D3DRS_AMBIENT, m_state.ambient);
    device->SetRenderState(D3DRS_COLORVERTEX, m_state.colorVertex);
    device->SetRenderState(D3DRS_CULLMODE, m_state.cullMode);
    device->SetRenderState(D3DRS_ZENABLE, m_state.zEnable);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, m_state.alphaBlendEnable);
    device->SetRenderState(D3DRS_SRCBLEND, m_state.srcBlend);
    device->SetRenderState(D3DRS_DESTBLEND, m_state.destBlend);

    // Restore texture stages
    for (DWORD i = 0; i < m_state.textureStages.size(); i++) {
        RestoreTextureStage(device, i, m_state.textureStages[i]);
    }

    // Release shader references
    if (m_state.vertexShader) {
        m_state.vertexShader->Release();
        m_state.vertexShader = nullptr;
    }
    if (m_state.pixelShader) {
        m_state.pixelShader->Release();
        m_state.pixelShader = nullptr;
    }

    m_isStored = false;
    FF_LOG("Device state restored");
}

void FixedFunctionState::SetupFixedFunction(
    IDirect3DDevice9* device,
    VertexFormat_t sourceFormat,
    IMaterial* material,
    bool enabled)
{
    FF_LOG(">>> SetupFixedFunction Called <<<");
    FF_LOG("Material: %s", material ? material->GetName() : "null");
    FF_LOG("Enabled: %s", enabled ? "YES" : "NO");

    if (!device || !material) {
        FF_WARN("Invalid device or material");
        return;
    }

    try {
        FF_LOG("Disabling shaders");
        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);

        FF_LOG("Setting FVF format");
        DWORD fvf = GetFVFFromSourceFormat(sourceFormat);
        device->SetFVF(fvf);

        FF_LOG("Setting up transforms");
        SetupTransforms(device, material);

        FF_LOG("Setting render states");
        // Force obvious visual changes
        HRESULT hr;
        
        hr = device->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
        FF_LOG("Set wireframe mode: %s", SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

        hr = device->SetRenderState(D3DRS_LIGHTING, FALSE);
        FF_LOG("Disable lighting: %s", SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

        // Force bright red color
        hr = device->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, 255, 0, 0));
        FF_LOG("Set color override: %s", SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

        hr = device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        FF_LOG("Set color op: %s", SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

        hr = device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        FF_LOG("Set color arg: %s", SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

        FF_LOG("Fixed function pipeline setup complete");
    }
    catch (const std::exception& e) {
        FF_WARN("Exception in SetupFixedFunction: %s", e.what());
    }
}

DWORD FixedFunctionState::GetFVFFromSourceFormat(VertexFormat_t format) {
    DWORD fvf = D3DFVF_XYZ; // Position is always present

    // Add normal if present
    if (format & FF_VERTEX_NORMAL)
        fvf |= D3DFVF_NORMAL;

    // Add diffuse color if present
    if (format & FF_VERTEX_COLOR)
        fvf |= D3DFVF_DIFFUSE;

    // Add specular color if present
    if (format & FF_VERTEX_SPECULAR)
        fvf |= D3DFVF_SPECULAR;

    // Handle texture coordinates
    int texCoordCount = 0;
    for (int i = 0; i < 8; i++) {
        unsigned int mask = FF_VERTEX_TEXCOORD0;
        if (format & (mask << i))
            texCoordCount++;
    }

    if (texCoordCount > 0)
        fvf |= (texCoordCount << D3DFVF_TEXCOUNT_SHIFT);

    return fvf;
}

void FixedFunctionState::SetupTransforms(IDirect3DDevice9* device, IMaterial* material) {
    // Get transforms from Source's material system
    D3DMATRIX worldMatrix = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    D3DMATRIX viewMatrix = worldMatrix;
    D3DMATRIX projMatrix = worldMatrix;

    // Set transforms
    device->SetTransform(D3DTS_WORLD, &worldMatrix);
    device->SetTransform(D3DTS_VIEW, &viewMatrix);
    device->SetTransform(D3DTS_PROJECTION, &projMatrix);
}

void FixedFunctionState::SetupTextureStages(IDirect3DDevice9* device, IMaterial* material) {
    // Setup first texture stage
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    // Disable other texture stages
    for (DWORD i = 1; i < 8; i++) {
        device->SetTextureStageState(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetTextureStageState(i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    }
}

void FixedFunctionState::SetupRenderStates(IDirect3DDevice9* device, IMaterial* material) {
    // Basic render states
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);

    // Setup alpha blending based on material properties
    bool isTranslucent = material->IsTranslucent();
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, isTranslucent ? TRUE : FALSE);
    if (isTranslucent) {
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    }
}

void FixedFunctionState::StoreTextureStage(IDirect3DDevice9* device, DWORD stage) {
    TextureStageState state;
    device->GetTextureStageState(stage, D3DTSS_COLOROP, &state.colorOp);
    device->GetTextureStageState(stage, D3DTSS_COLORARG1, &state.colorArg1);
    device->GetTextureStageState(stage, D3DTSS_COLORARG2, &state.colorArg2);
    device->GetTextureStageState(stage, D3DTSS_ALPHAOP, &state.alphaOp);
    device->GetTextureStageState(stage, D3DTSS_ALPHAARG1, &state.alphaArg1);
    device->GetTextureStageState(stage, D3DTSS_ALPHAARG2, &state.alphaArg2);
    device->GetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, &state.texCoordIndex);
    device->GetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, &state.textureTransformFlags);
    m_state.textureStages.push_back(state);
}

void FixedFunctionState::RestoreTextureStage(
    IDirect3DDevice9* device, 
    DWORD stage, 
    const TextureStageState& state)
{
    device->SetTextureStageState(stage, D3DTSS_COLOROP, state.colorOp);
    device->SetTextureStageState(stage, D3DTSS_COLORARG1, state.colorArg1);
    device->SetTextureStageState(stage, D3DTSS_COLORARG2, state.colorArg2);
    device->SetTextureStageState(stage, D3DTSS_ALPHAOP, state.alphaOp);
    device->SetTextureStageState(stage, D3DTSS_ALPHAARG1, state.alphaArg1);
    device->SetTextureStageState(stage, D3DTSS_ALPHAARG2, state.alphaArg2);
    device->SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, state.texCoordIndex);
    device->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, state.textureTransformFlags);
}