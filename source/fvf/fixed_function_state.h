#pragma once
#include <d3d9.h>
#include <d3dx9.h>
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "vertex_format.h"
#include <vector>

// Forward declare
class IMaterial;
extern IMaterialSystem* materials;

class FixedFunctionState {
public:
    FixedFunctionState();
    ~FixedFunctionState();

    void Store(IDirect3DDevice9* device);
    void Restore(IDirect3DDevice9* device);
    void SetupFixedFunction(
        IDirect3DDevice9* device, 
        VertexFormat_t sourceFormat, 
        IMaterial* material,
        bool enabled);  // Add enabled parameter

private:
    struct TextureStageState {
        DWORD colorOp;
        DWORD colorArg1;
        DWORD colorArg2;
        DWORD alphaOp;
        DWORD alphaArg1;
        DWORD alphaArg2;
        DWORD texCoordIndex;
        DWORD textureTransformFlags;
    };

    struct StoredState {
        // Shaders
        IDirect3DVertexShader9* vertexShader = nullptr;
        IDirect3DPixelShader9* pixelShader = nullptr;
        DWORD fvf = 0;

        // Matrices
        D3DMATRIX world;
        D3DMATRIX view;
        D3DMATRIX projection;

        // Render states
        DWORD lighting;
        DWORD ambient;
        DWORD colorVertex;
        DWORD cullMode;
        DWORD zEnable;
        DWORD alphaBlendEnable;
        DWORD srcBlend;
        DWORD destBlend;

        // Texture stages (store state for each stage)
        std::vector<TextureStageState> textureStages;
    };

    StoredState m_state;
    bool m_isStored = false;

    // Helper functions
    DWORD GetFVFFromSourceFormat(VertexFormat_t format);
    void SetupTransforms(IDirect3DDevice9* device, IMaterial* material);
    void SetupTextureStages(IDirect3DDevice9* device, IMaterial* material);
    void SetupRenderStates(IDirect3DDevice9* device, IMaterial* material);
    void SetupTexture(IDirect3DDevice9* device, IMaterial* material, int stage);
    void StoreTextureStage(IDirect3DDevice9* device, DWORD stage);
    void RestoreTextureStage(IDirect3DDevice9* device, DWORD stage, const TextureStageState& state);
};