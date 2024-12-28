#include "fixed_function_renderer.h"
#include "fixed_function_state.h"
#include "vertex_format.h"
#include "material_util.h"
#include <tier0/dbg.h>

// Debug helpers - expanded
#define FF_LOG(fmt, ...) Msg("[Fixed Function] " fmt "\n", ##__VA_ARGS__)
#define FF_WARN(fmt, ...) Warning("[Fixed Function] " fmt "\n", ##__VA_ARGS__)

void RenderStats::Reset() {
    totalDrawCalls = 0;
    fixedFunctionDrawCalls = 0;
    lastStatsTime = 0.0f;
}

void RenderStats::LogIfNeeded() {
    float currentTime = Plat_FloatTime();
    if (currentTime - lastStatsTime > 5.0f) {  // Log every 5 seconds
        FF_LOG("Stats - Total Draws: %d, FF Draws: %d (%.1f%%)",
            totalDrawCalls, fixedFunctionDrawCalls,
            totalDrawCalls > 0 ? (fixedFunctionDrawCalls * 100.0f / totalDrawCalls) : 0.0f);
        Reset();
        lastStatsTime = currentTime;
    }
}

FixedFunctionRenderer& FixedFunctionRenderer::Instance() {
    static FixedFunctionRenderer instance;
    return instance;
}

void FixedFunctionRenderer::Initialize(IDirect3DDevice9* device) {
    if (!device) {
        FF_WARN("Null device in Initialize");
        return;
    }

    m_device = device;
    m_stats.Reset();
    
    // Get D3D9 vtable and hook DrawIndexedPrimitive
    void** vftable = *reinterpret_cast<void***>(device);
    
    try {
        // Hook DrawIndexedPrimitive
        Detouring::Hook::Target target(&vftable[82]);
        m_drawHook.Create(target, DrawIndexedPrimitive_Detour);
        m_originalDrawIndexedPrimitive = m_drawHook.GetTrampoline<DrawIndexedPrimitive_t>();
        m_drawHook.Enable();
        
        // Create state manager
        m_stateManager = std::make_unique<FixedFunctionState>();
        
        FF_LOG("Successfully initialized fixed function renderer");
    }
    catch (const std::exception& e) {
        FF_WARN("Failed to initialize: %s", e.what());
        return;
    }
}

void FixedFunctionRenderer::Shutdown() {
    if (m_drawHook.IsEnabled()) {
        m_drawHook.Disable();
    }
    
    m_stateManager.reset();
    m_device = nullptr;
    m_originalDrawIndexedPrimitive = nullptr;
    
    FF_LOG("Shutdown complete");
}

bool FixedFunctionRenderer::RenderWithFixedFunction(
    IDirect3DDevice9* device,
    IMaterial* material,
    VertexFormat_t format,
    D3DPRIMITIVETYPE primType,
    INT baseVertexIndex,
    UINT minVertexIndex,
    UINT numVertices,
    UINT startIndex,
    UINT primCount)
{
    if (!m_stateManager) {
        FF_WARN("No state manager available");
        return false;
    }

    try {
        // Store current state
        m_stateManager->Store(device);

        // Setup fixed function pipeline
        m_stateManager->SetupFixedFunction(device, format, material);

        // Render
        HRESULT hr = device->DrawIndexedPrimitive(
            primType, baseVertexIndex, minVertexIndex,
            numVertices, startIndex, primCount);

        // Restore state
        m_stateManager->Restore(device);

        if (FAILED(hr)) {
            FF_WARN("Draw failed with error 0x%08x", hr);
            return false;
        }

        return true;
    }
    catch (const std::exception& e) {
        FF_WARN("Exception in RenderWithFixedFunction: %s", e.what());
        m_stateManager->Restore(device);  // Try to restore state
        return false;
    }
}

HRESULT WINAPI FixedFunctionRenderer::DrawIndexedPrimitive_Detour(
    IDirect3DDevice9* device,
    D3DPRIMITIVETYPE PrimitiveType,
    INT BaseVertexIndex,
    UINT MinVertexIndex,
    UINT NumVertices,
    UINT StartIndex,
    UINT PrimitiveCount)
{
    auto& instance = Instance();  // Get instance first
    instance.m_stats.totalDrawCalls++;

    // Get current material
    IMaterial* material = materials->GetRenderContext()->GetCurrentMaterial();
    if (!material) {
        return instance.m_originalDrawIndexedPrimitive(
            device, PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
    }

    // For testing: Use a basic vertex format that should work with most geometry
    VertexFormat_t format = 
        FF_VERTEX_POSITION | 
        FF_VERTEX_NORMAL | 
        FF_VERTEX_TEXCOORD0;
    
    static float lastMaterialLog = 0.0f;
    float currentTime = Plat_FloatTime();

    // Add more detailed logging for testing
    if (currentTime - lastMaterialLog > 1.0f) {
        FF_LOG("Draw call info:");
        FF_LOG("  Material: %s", material->GetName());
        FF_LOG("  Shader: %s", material->GetShaderName());
        FF_LOG("  Vertices: %d, Primitives: %d", NumVertices, PrimitiveCount);
        FF_LOG("  Using test format: Position + Normal + TexCoord");
        lastMaterialLog = currentTime;
    }

    // Check if we should use fixed function
    if (MaterialUtil::ShouldUseFixedFunction(material)) {
        instance.m_stats.fixedFunctionDrawCalls++;

        bool success = instance.RenderWithFixedFunction(
            device, material, format,
            PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices,
            StartIndex, PrimitiveCount);

        if (success) {
            return D3D_OK;
        }
        
        // Fall through to original path if fixed function failed
        FF_WARN("Fixed function render failed, using original path");
    }

    // Update and log stats
    instance.m_stats.LogIfNeeded();

    // Use original path
    return instance.m_originalDrawIndexedPrimitive(
        device, PrimitiveType, BaseVertexIndex,
        MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
}