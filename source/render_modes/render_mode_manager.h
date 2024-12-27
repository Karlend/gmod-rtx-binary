#pragma once
#include <d3d9.h>
#include <unordered_map>
#include <string>
#include <Windows.h>

class RenderModeManager {
public:
    static RenderModeManager& Instance();

    void Initialize(IDirect3DDevice9* device);
    void Shutdown();

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

    // Helper functions
    bool IsWorldDrawing() const;
    bool IsModelDrawing() const;
    void LogMessage(const char* format, ...);
    IDirect3DVertexDeclaration9* CreateFVFDeclaration(DWORD fvf);
    void ClearFVFCache();

    CRITICAL_SECTION m_cs;
};