// fixed_function_state.cpp
#include "fixed_function_state.h"
#include <tier0/dbg.h>
#include "ff_logging.h"
#include <d3d9.h>

FixedFunctionState::FixedFunctionState() {
    FF_LOG("Creating FixedFunctionState instance");
    m_isStored = false;
}

FixedFunctionState::~FixedFunctionState() {
    if (m_state.vertexShader) m_state.vertexShader->Release();
    if (m_state.pixelShader) m_state.pixelShader->Release();
}

void FixedFunctionState::SetupModelStates(IDirect3DDevice9* device, IMaterial* material, VertexFormat_t format)
{
    device->SetRenderState(D3DRS_LIGHTING, TRUE);
    device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_ARGB(255, 128, 128, 128));
    device->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    device->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
    device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);

    if (format & FF_VERTEX_BONES) {
        device->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_3WEIGHTS);
        device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
        SetupBoneMatrices(device, material);
    }

    D3DMATERIAL9 mtrl = {};
    mtrl.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
    mtrl.Ambient = { 0.5f, 0.5f, 0.5f, 1.0f };
    mtrl.Specular = { 0.2f, 0.2f, 0.2f, 1.0f };
    mtrl.Power = 8.0f;
    device->SetMaterial(&mtrl);
}

void FixedFunctionState::SetupGUIStates(IDirect3DDevice9* device)
{
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
}

void FixedFunctionState::SetupWorldStates(IDirect3DDevice9* device)
{
    device->SetRenderState(D3DRS_LIGHTING, TRUE);
    device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_ARGB(255, 128, 128, 128));
    device->SetRenderState(D3DRS_ZENABLE, TRUE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
}

void FixedFunctionState::SetupBoneMatrices(IDirect3DDevice9* device, IMaterial* material)
{
    IMaterialVar* boneVar = material->FindVar("$numbones", nullptr);
    int numBones = boneVar ? boneVar->GetIntValue() : 0;
    FF_LOG("  Setting up %d bone matrices", numBones);

    for (int i = 0; i < numBones && i < 96; i++) {
        D3DMATRIX boneMatrix = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        device->SetTransform(D3DTS_WORLDMATRIX(i), &boneMatrix);
    }
}

void FixedFunctionState::SetupTextures(IDirect3DDevice9* device, IMaterial* material)
{
    IMaterialVar* baseTexture = material->FindVar("$basetexture", nullptr);
    if (!baseTexture || !baseTexture->IsDefined()) {
        FF_LOG("No base texture for material %s", material->GetName());
        device->SetTexture(0, nullptr);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        return;
    }

    void* texHandle = nullptr;
    try {
        texHandle = baseTexture->GetTextureValue();
    }
    catch (...) {
        FF_WARN("Failed to get texture for material %s", material->GetName());
        return;
    }

    if (texHandle) {
        IDirect3DBaseTexture9* d3dtex = static_cast<IDirect3DBaseTexture9*>(texHandle);
        device->SetTexture(0, d3dtex);
        
        // Set up texture stage states
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

        // Set up sampling states
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    }

    // Disable unused texture stages
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

bool FixedFunctionState::FindAndSetTexture(IDirect3DDevice9* device, IMaterial* material) {
    if (!device || !material) {
        FF_WARN("Invalid device or material in FindAndSetTexture");
        return false;
    }

    FF_LOG("Attempting to set texture for material: %s", material->GetName());

    try {
        // Safely get texture var
        IMaterialVar* textureVar = material->FindVar("$basetexture", nullptr);
        if (!textureVar) {
            FF_LOG("No $basetexture var for material %s", material->GetName());
            goto USE_FALLBACK;
        }

        FF_LOG("Found $basetexture var for %s", material->GetName());

        if (!textureVar->IsDefined()) {
            FF_LOG("$basetexture not defined for material %s", material->GetName());
            goto USE_FALLBACK;
        }

        // Safely get texture handle
        void* texHandle = nullptr;
        try {
            FF_LOG("Getting texture value for %s", material->GetName());
            texHandle = textureVar->GetTextureValue();
            FF_LOG("Got texture handle: %p", texHandle);
        }
        catch (...) {
            FF_WARN("Exception getting texture value for material %s", material->GetName());
            goto USE_FALLBACK;
        }

        if (!texHandle) {
            FF_LOG("Null texture handle for material %s", material->GetName());
            goto USE_FALLBACK;
        }

        // Safely cast and set texture
        IDirect3DBaseTexture9* d3dtex = static_cast<IDirect3DBaseTexture9*>(texHandle);
        if (!d3dtex) {
            FF_LOG("Failed to cast texture for material %s", material->GetName());
            goto USE_FALLBACK;
        }

        FF_LOG("Checking texture type for %s", material->GetName());
        // Verify texture type
        D3DRESOURCETYPE texType = d3dtex->GetType();
        FF_LOG("Texture type: %d", texType);
        
        if (texType != D3DRTYPE_TEXTURE && texType != D3DRTYPE_CUBETEXTURE) {
            FF_WARN("Invalid texture type %d for material %s", texType, material->GetName());
            goto USE_FALLBACK;
        }

        FF_LOG("Setting texture for %s", material->GetName());
        // Set texture with error checking
        HRESULT hr = device->SetTexture(0, d3dtex);
        if (FAILED(hr)) {
            FF_WARN("Failed to set texture for material %s (HRESULT: 0x%x)", 
                material->GetName(), hr);
            goto USE_FALLBACK;
        }

        FF_LOG("Successfully set texture for %s", material->GetName());

        // Setup texture stages safely
        FF_LOG("Setting up texture stages for %s", material->GetName());
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

        return true;

    USE_FALLBACK:
        FF_LOG("Using fallback rendering for %s", material->GetName());
        // Safe fallback state
        device->SetTexture(0, nullptr);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

        // Set a default material color
        D3DMATERIAL9 mtrl;
        ZeroMemory(&mtrl, sizeof(mtrl));
        mtrl.Diffuse = { 0.7f, 0.7f, 0.7f, 1.0f };
        mtrl.Ambient = { 0.3f, 0.3f, 0.3f, 1.0f };
        device->SetMaterial(&mtrl);

        return false;
    }
    catch (const std::exception& e) {
        FF_WARN("Exception in FindAndSetTexture for %s: %s", 
            material->GetName(), e.what());
        return false;
    }
    catch (...) {
        FF_WARN("Unknown exception in FindAndSetTexture for %s", 
            material->GetName());
        return false;
    }
}


void FixedFunctionState::Store(IDirect3DDevice9* device) {
    if (!device) return;

    // Store shaders
    device->GetVertexShader(&m_state.vertexShader);
    device->GetPixelShader(&m_state.pixelShader);
    device->GetFVF(&m_state.fvf);

    FF_LOG("  Original FVF: 0x%x", m_state.fvf);
    FF_LOG("  Vertex Shader: %p", m_state.vertexShader);
    FF_LOG("  Pixel Shader: %p", m_state.pixelShader);

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

    FF_LOG("Restoring device state...");

    // Restore vertex buffer first
    if (m_state.vertexBuffer) {
        device->SetStreamSource(0, m_state.vertexBuffer, 0, m_state.stride);
        m_state.vertexBuffer->Release();
        m_state.vertexBuffer = nullptr;
    }

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

    try {
        // Store original state
        device->GetVertexShader(&m_state.vertexShader);
        device->GetPixelShader(&m_state.pixelShader);
        device->GetFVF(&m_state.fvf);

        // Disable shaders first
        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);

        // Get current vertex buffer
        IDirect3DVertexBuffer9* vb = nullptr;
        UINT vbOffset = 0, stride = 0;
        device->GetStreamSource(0, &vb, &vbOffset, &m_state.stride);

        if (!vb) {
            FF_WARN("No vertex buffer bound");
            return;
        }

        // Keep track of the original buffer
        m_state.vertexBuffer = vb;

        // Calculate FVF based on source format
        DWORD fvf = D3DFVF_XYZ;
        
        if (sourceFormat & FF_VERTEX_NORMAL)
            fvf |= D3DFVF_NORMAL;
        
        if (sourceFormat & FF_VERTEX_COLOR)
            fvf |= D3DFVF_DIFFUSE;

        int numTexCoords = 0;
        if (sourceFormat & FF_VERTEX_TEXCOORD0) numTexCoords++;
        if (sourceFormat & FF_VERTEX_TEXCOORD1) numTexCoords++;
        
        if (numTexCoords > 0)
            fvf |= (numTexCoords << D3DFVF_TEXCOUNT_SHIFT);

        FF_LOG("Setting FVF: 0x%x", fvf);
        device->SetFVF(fvf);

        // Set up render states
        device->SetRenderState(D3DRS_LIGHTING, TRUE);
        device->SetRenderState(D3DRS_ZENABLE, TRUE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);

        // Setup material
        D3DMATERIAL9 mtrl;
        ZeroMemory(&mtrl, sizeof(mtrl));
        mtrl.Diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
        mtrl.Ambient = {0.5f, 0.5f, 0.5f, 1.0f};
        device->SetMaterial(&mtrl);

        // Setup light
        D3DLIGHT9 light;
        ZeroMemory(&light, sizeof(light));
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
        light.Direction = {0.0f, -1.0f, -1.0f};
        device->SetLight(0, &light);
        device->LightEnable(0, TRUE);

        // Handle material texture
        if (material) {
            IMaterialVar* baseTexture = material->FindVar("$basetexture", nullptr);
            if (baseTexture && baseTexture->IsTexture()) {
                void* texHandle = baseTexture->GetTextureValue();
                if (texHandle) {
                    IDirect3DBaseTexture9* d3dtex = static_cast<IDirect3DBaseTexture9*>(texHandle);
                    device->SetTexture(0, d3dtex);
                    
                    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
                    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
                    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
                    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                }
            }
        }

        // Disable unused texture stages
        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        FF_LOG("Setup complete");
    }
    catch (const std::exception& e) {
        FF_WARN("Exception in SetupFixedFunction: %s", e.what());
    }
}

DWORD FixedFunctionState::GetFVFFromSourceFormat(VertexFormat_t format) {
    DWORD fvf = D3DFVF_XYZ; // Position is always present

    FF_LOG("Converting format 0x%x to FVF", format);

    // Add normal if present
    if (format & FF_VERTEX_NORMAL) {
        fvf |= D3DFVF_NORMAL;
        FF_LOG("  Added normal");
    }

    // Add diffuse color if present
    if (format & FF_VERTEX_COLOR) {
        fvf |= D3DFVF_DIFFUSE;
        FF_LOG("  Added color");
    }

    // Add specular color if present
    if (format & FF_VERTEX_SPECULAR) {
        fvf |= D3DFVF_SPECULAR;
        FF_LOG("  Added specular");
    }

    // Handle bone weights for skinned meshes
    if (format & FF_VERTEX_BONES) {
        FF_LOG("  Adding bone weights");
        fvf &= ~D3DFVF_XYZRHW;  // Remove XYZRHW if present
        fvf |= D3DFVF_XYZB4;    // Add room for 4 blend weights
        fvf |= D3DFVF_LASTBETA_UBYTE4; // Specify blend indices format
    }

    // Handle texture coordinates
    int texCoordCount = 0;
    for (int i = 0; i < 8; i++) {
        if (format & (FF_VERTEX_TEXCOORD0 << i))
            texCoordCount++;
    }

    if (texCoordCount > 0) {
        fvf |= (texCoordCount << D3DFVF_TEXCOUNT_SHIFT);
        FF_LOG("  Added %d texture coordinates", texCoordCount);
    }

    FF_LOG("Final FVF: 0x%x", fvf);
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