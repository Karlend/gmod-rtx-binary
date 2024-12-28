#pragma once
#include <d3d9.h>
#include <unordered_map>
#include <string>
#include <Windows.h>
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "mathlib/mathlib.h"
#include "detouring/hook.hpp"
#include "render_state_logger.h"
#include <mutex>

class RenderModeManager {
public:
    static RenderModeManager& Instance();

    // Function pointer types
    typedef HRESULT (WINAPI *SetFVF_t)(IDirect3DDevice9*, DWORD);
    typedef HRESULT (WINAPI *SetVertexDeclaration_t)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*);
    typedef HRESULT (WINAPI *SetStreamSource_t)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
    typedef HRESULT (WINAPI *DrawPrimitive_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
    typedef HRESULT (WINAPI *DrawIndexedPrimitive_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);

    // Detour functions
    static HRESULT WINAPI SetFVF_Detour(IDirect3DDevice9* device, DWORD FVF);
    static HRESULT WINAPI SetVertexDeclaration_Detour(IDirect3DDevice9* device, IDirect3DVertexDeclaration9* decl);
    static HRESULT WINAPI SetStreamSource_Detour(IDirect3DDevice9* device, UINT StreamNumber,
        IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride);
    static HRESULT WINAPI DrawPrimitive_Detour(IDirect3DDevice9* device, D3DPRIMITIVETYPE PrimitiveType,
        UINT StartVertex, UINT PrimitiveCount);
    static HRESULT WINAPI DrawIndexedPrimitive_Detour(IDirect3DDevice9* device, D3DPRIMITIVETYPE PrimitiveType,
        INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount);

    // Function pointers accessed by detours
    SetFVF_t m_originalSetFVF;
    SetVertexDeclaration_t m_originalSetVertexDeclaration;
    SetStreamSource_t m_originalSetStreamSource;
    DrawPrimitive_t m_originalDrawPrimitive;
    DrawIndexedPrimitive_t m_originalDrawIndexedPrimitive;

    // Initialization and shutdown
    void Initialize(IDirect3DDevice9* device);
    void Shutdown();

    // Public helpers needed by detours
    IDirect3DVertexDeclaration9* CreateFVFDeclaration(DWORD fvf);
    bool ShouldUseFVF() const;
    UINT GetFVFStride(DWORD fvf);
    void LogMessage(const char* format, ...);
    bool IsWorldDrawing() const;
    bool IsModelDrawing() const;

    // Buffer management
    struct VertexBufferInfo {
        DWORD fvf;
        UINT stride;
        bool isModel;
        bool isWorld;
    };

    bool ValidateVertexBuffer(IDirect3DVertexBuffer9* buffer, DWORD fvf);
    void RestoreState();
    void ClearVertexBufferCache();

    // FVF control
    void EnableFVFForWorld(bool enable);
    void EnableFVFForModels(bool enable);

private:
    RenderModeManager();
    ~RenderModeManager();

    // State tracking
    bool m_worldFVFEnabled;
    bool m_modelsFVFEnabled;
    bool m_initialized;
    IDirect3DDevice9* m_device;
    CRITICAL_SECTION m_cs;

    // Cache structures
    struct FVFFormat {
        DWORD fvf;
        IDirect3DVertexDeclaration9* declaration;
    };

    // Caches
    std::unordered_map<DWORD, FVFFormat> m_fvfCache;
    std::unordered_map<IDirect3DVertexBuffer9*, VertexBufferInfo> m_vertexBufferCache;
    std::vector<Detouring::Hook*> m_hooks;

    // Cache management
    void ClearFVFCache();
};

// Declare external materials system pointer
extern IMaterialSystem* materials;