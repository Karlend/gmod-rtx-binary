// fixed_function_state.cpp
#include "fixed_function_state.h"
#include <tier0/dbg.h>
#include "ff_logging.h"

FixedFunctionState::FixedFunctionState() {
    FF_LOG("Creating FixedFunctionState instance");
    m_isStored = false;
}

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
    static float lastLogTime = 0.0f;
    float currentTime = Plat_FloatTime();
    bool shouldLog = (currentTime - lastLogTime > 1.0f);
    
    if (shouldLog) {
        FF_LOG(">>> SetupFixedFunction Called <<<");
        FF_LOG("Material: %s", material ? material->GetName() : "null");
        lastLogTime = currentTime;
    }

    if (!device || !material) {
        FF_WARN("Invalid device or material");
        return;
    }

    try {
        // Disable shaders
        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);

        // Setup vertex format
        DWORD fvf = GetFVFFromSourceFormat(sourceFormat);
        device->SetFVF(fvf);

        // Setup transforms
        SetupTransforms(device, material);

        // Basic render states
        device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        device->SetRenderState(D3DRS_LIGHTING, TRUE);
        device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_ARGB(255, 128, 128, 128));

        // Set up basic material properties
        D3DMATERIAL9 mtrl;
        ZeroMemory(&mtrl, sizeof(mtrl));
        mtrl.Diffuse = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
        mtrl.Ambient = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
        device->SetMaterial(&mtrl);

        // Setup basic light
        device->SetRenderState(D3DRS_LIGHTING, TRUE);
        D3DLIGHT9 light;
        ZeroMemory(&light, sizeof(light));
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
        light.Direction = D3DXVECTOR3(0.0f, 0.0f, -1.0f);
        device->SetLight(0, &light);
        device->LightEnable(0, TRUE);

        // Color and alpha blending setup
        device->SetRenderState(D3DRS_COLORVERTEX, TRUE);
        device->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
        device->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);

        // Setup texture
        IMaterialVar* baseTexture = material->FindVar("$basetexture", nullptr);
        if (baseTexture && !baseTexture->IsDefined()) {
            void* texHandle = baseTexture->GetTextureValue();
            if (texHandle) {
                IDirect3DBaseTexture9* d3dtex = static_cast<IDirect3DBaseTexture9*>(texHandle);
                if (d3dtex) {
                    device->SetTexture(0, d3dtex);
                    
                    // Set up texture stage states
                    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
                    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
                    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

                    // Texture addressing and filtering
                    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
                    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
                    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
                }
            } else {
                // No texture, use fixed color
                device->SetTexture(0, nullptr);
                device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
                device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            }
        }

        // Disable unused texture stages
        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        // Setup alpha blending
        bool isTranslucent = material->IsTranslucent();
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, isTranslucent ? TRUE : FALSE);
        if (isTranslucent) {
            device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        }

        if (shouldLog) {
            FF_LOG("Fixed function pipeline setup complete for material: %s", material->GetName());
        }
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
        if (format & (FF_VERTEX_TEXCOORD0 << i))
            texCoordCount++;
    }

    if (texCoordCount > 0)
        fvf |= (texCoordCount << D3DFVF_TEXCOUNT_SHIFT);

    // Handle skinned meshes
    if (format & FF_VERTEX_BONES) {
        // Set up to use indexed vertex blending
        fvf |= D3DFVF_XYZB4; // Use 4 blend indices
        fvf |= D3DFVF_LASTBETA_UBYTE4; // Use unsigned bytes for indices
    }

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