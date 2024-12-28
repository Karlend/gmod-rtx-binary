#pragma once
#include <d3d9.h>
#include <memory>
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "vertex_format.h"
#include "detouring/hook.hpp"

#define FF_LOG(fmt, ...) Msg("[Fixed Function] " fmt "\n", ##__VA_ARGS__)
#define FF_WARN(fmt, ...) Warning("[Fixed Function] " fmt "\n", ##__VA_ARGS__)


// Forward declare
extern IMaterialSystem* materials;

class FixedFunctionState;

struct RenderStats {
    int totalDrawCalls = 0;
    int fixedFunctionDrawCalls = 0;
    float lastStatsTime = 0.0f;
    void Reset();
    void LogIfNeeded();
};

class FixedFunctionRenderer {
public:
    static FixedFunctionRenderer& Instance();
    
    void Initialize(IDirect3DDevice9* device);
    void Shutdown();

    void SetEnabled(bool enable) {
        m_enabled = enable;
        FF_LOG("Renderer %s", enable ? "enabled" : "disabled");
    }

private:
    IDirect3DDevice9* m_device = nullptr;
    std::unique_ptr<FixedFunctionState> m_stateManager;
    
    // Hook related
    typedef HRESULT (WINAPI *DrawIndexedPrimitive_t)(
        IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, 
        UINT, UINT, UINT, UINT);
    
    DrawIndexedPrimitive_t m_originalDrawIndexedPrimitive = nullptr;
    Detouring::Hook m_drawHook;

    // Main render function
    bool RenderWithFixedFunction(
        IDirect3DDevice9* device,
        IMaterial* material,
        VertexFormat_t format,
        D3DPRIMITIVETYPE primType,
        INT baseVertexIndex,
        UINT minVertexIndex,
        UINT numVertices,
        UINT startIndex,
        UINT primCount);

    // Hook implementation
    static HRESULT WINAPI DrawIndexedPrimitive_Detour(
        IDirect3DDevice9* device,
        D3DPRIMITIVETYPE PrimitiveType,
        INT BaseVertexIndex,
        UINT MinVertexIndex,
        UINT NumVertices,
        UINT StartIndex,
        UINT PrimitiveCount);

        RenderStats m_stats;
        bool m_enabled = false;
};