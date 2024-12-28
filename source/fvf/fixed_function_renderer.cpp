#include "fixed_function_renderer.h"
#include "fixed_function_state.h"
#include "vertex_format.h"
#include "material_util.h"
#include <tier0/dbg.h>

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
    FF_LOG(">>> RenderWithFixedFunction Called <<<");

    if (!m_stateManager) {
        FF_WARN("No state manager available");
        return false;
    }

    try {
        FF_LOG("Storing current state");
        m_stateManager->Store(device);

        FF_LOG("Setting up fixed function state");
        m_stateManager->SetupFixedFunction(device, format, material, true);

        FF_LOG("Performing draw call");
        HRESULT hr = device->DrawIndexedPrimitive(
            primType, baseVertexIndex, minVertexIndex,
            numVertices, startIndex, primCount);

        FF_LOG("Restoring state");
        m_stateManager->Restore(device);

        if (FAILED(hr)) {
            FF_WARN("Draw failed with error 0x%08x", hr);
            return false;
        }

        FF_LOG("Fixed function render completed successfully");
        return true;
    }
    catch (const std::exception& e) {
        FF_WARN("Exception in RenderWithFixedFunction: %s", e.what());
        m_stateManager->Restore(device);
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
    static float lastDebugTime = 0.0f;
    float currentTime = Plat_FloatTime();
    auto& instance = Instance();
    
    // Debug Print #1 - Hook Interception
    if (currentTime - lastDebugTime > 1.0f) {
        FF_LOG(">>> Draw Hook Called <<<");
        FF_LOG("Fixed Function Enabled: %s", instance.m_enabled ? "YES" : "NO");
        FF_LOG("Primitive Count: %d, Vertices: %d", PrimitiveCount, NumVertices);
        lastDebugTime = currentTime;
    }

    instance.m_stats.totalDrawCalls++;

    // If fixed function is disabled, use original path
    if (!instance.m_enabled) {
        FF_LOG("Fixed Function disabled, using original path");
        return instance.m_originalDrawIndexedPrimitive(
            device, PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
    }

    // Get current material
    IMaterial* material = materials->GetRenderContext()->GetCurrentMaterial();
    if (!material) {
        FF_LOG("No material found, using original path");
        return instance.m_originalDrawIndexedPrimitive(
            device, PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
    }

    // Debug Print #2 - Material Info
    if (currentTime - lastDebugTime > 1.0f) {
        FF_LOG("Material: %s", material->GetName());
        FF_LOG("Shader: %s", material->GetShaderName());
    }

    // Check if we should use fixed function
    if (MaterialUtil::ShouldUseFixedFunction(material)) {
        FF_LOG(">>> Using Fixed Function Path <<<");
        instance.m_stats.fixedFunctionDrawCalls++;

        VertexFormat_t format = 
            FF_VERTEX_POSITION | 
            FF_VERTEX_NORMAL | 
            FF_VERTEX_TEXCOORD0;

        bool success = instance.RenderWithFixedFunction(
            device, material, format,
            PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices,
            StartIndex, PrimitiveCount);

        if (success) {
            FF_LOG("Fixed Function render successful");
            return D3D_OK;
        }
        
        FF_WARN("Fixed function render failed, using original path");
    }

    // Update and log stats
    instance.m_stats.LogIfNeeded();

    FF_LOG("Using original render path");
    return instance.m_originalDrawIndexedPrimitive(
        device, PrimitiveType, BaseVertexIndex,
        MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
}