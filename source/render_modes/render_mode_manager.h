#pragma once
#include <d3d9.h>
#include <unordered_map>
#include <string>
#include <Windows.h>
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "mathlib/mathlib.h"

class RenderModeManager {
public:
    static RenderModeManager& Instance();

    void Initialize(IDirect3DDevice9* device);
    void Shutdown();

    struct VertexBufferInfo {
    DWORD fvf;
    UINT stride;
    bool isModel;
    bool isWorld;
    };

    bool ValidateVertexBuffer(IDirect3DVertexBuffer9* buffer, DWORD fvf);
    UINT GetFVFStride(DWORD fvf);
    void RestoreState();
    void ClearVertexBufferCache();

    // Control which elements use FVF
    void EnableFVFForWorld(bool enable);
    void EnableFVFForModels(bool enable);
    
    // Check if an element should use FVF
    bool ShouldUseFVF() const;

private:
    RenderModeManager();
    ~RenderModeManager();

    // Original D3D9 functions we'll hook
    typedef HRESULT (WINAPI *SetFVF_t)(IDirect3DDevice9*, DWORD);
    typedef HRESULT (WINAPI *SetVertexDeclaration_t)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*);
    typedef HRESULT (WINAPI *SetStreamSource_t)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
    SetStreamSource_t m_originalSetStreamSource;
    
    SetFVF_t m_originalSetFVF;
    SetVertexDeclaration_t m_originalSetVertexDeclaration;

    // State tracking
    bool m_worldFVFEnabled;
    bool m_modelsFVFEnabled;
    bool m_initialized;
    IDirect3DDevice9* m_device;
    
    // FVF format cache
    struct FVFFormat {
        DWORD fvf;
        IDirect3DVertexDeclaration9* declaration;
    };
    std::unordered_map<DWORD, FVFFormat> m_fvfCache;
    std::unordered_map<IDirect3DVertexBuffer9*, VertexBufferInfo> m_vertexBufferCache;

    // Helper functions
    bool IsWorldDrawing() const;
    bool IsModelDrawing() const;
    void LogMessage(const char* format, ...);
    IDirect3DVertexDeclaration9* CreateFVFDeclaration(DWORD fvf);
    void ClearFVFCache();
    
    CRITICAL_SECTION m_cs;
    friend HRESULT WINAPI SetFVF_Detour(IDirect3DDevice9* device, DWORD FVF);
    friend HRESULT WINAPI SetVertexDeclaration_Detour(IDirect3DDevice9* device, IDirect3DVertexDeclaration9* decl);
    friend HRESULT WINAPI SetStreamSource_Detour(IDirect3DDevice9* device, UINT StreamNumber, 
        IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride);
};