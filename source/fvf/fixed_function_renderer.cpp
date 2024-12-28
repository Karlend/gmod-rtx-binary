#include "fixed_function_renderer.h"
#include "fixed_function_state.h"
#include "vertex_format.h"
#include "material_util.h"
#include <tier0/dbg.h>
#include "ff_logging.h"
#include "interface.h"
#include "filesystem.h"
#include "materialsystem/imaterialsystem.h"

static IMaterialSystem* g_pMaterialSystem = nullptr;

FixedFunctionRenderer::DrawIndexedPrimitive_t FixedFunctionRenderer::s_originalDrawIndexedPrimitive = nullptr;
Detouring::Hook FixedFunctionRenderer::s_drawHook;

void FixedFunctionRenderer::SetEnabled(bool enable) {
    m_enabled = enable;
    FF_LOG("Renderer %s", enable ? "enabled" : "disabled");
    
    if (enable && !m_stateManager) {
        FF_LOG("State manager missing, attempting to create...");
        try {
            m_stateManager = std::make_unique<FixedFunctionState>();
            if (m_stateManager) {
                FF_LOG("State manager created successfully during enable");
            } else {
                FF_WARN("Failed to create state manager during enable");
            }
        }
        catch (const std::exception& e) {
            FF_WARN("Exception creating state manager during enable: %s", e.what());
        }
    }
}

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

    // Get MaterialSystem interface
    CreateInterfaceFn factory = Sys_GetFactory("materialsystem.dll");
    if (factory) {
        g_pMaterialSystem = (IMaterialSystem*)factory(MATERIAL_SYSTEM_INTERFACE_VERSION, NULL);
        if (g_pMaterialSystem) {
            FF_LOG("MaterialSystem interface acquired");
            materials = g_pMaterialSystem; // Set the global materials pointer
        } else {
            FF_WARN("Failed to get MaterialSystem interface");
        }
    } else {
        FF_WARN("Failed to get materialsystem.dll factory");
    }

    FF_LOG("Initializing with device: %p", device);
    m_device = device;
    m_stats.Reset();

    // Create state manager first
    try {
        FF_LOG("Creating state manager...");
        m_stateManager = std::make_unique<FixedFunctionState>();
        if (!m_stateManager) {
            FF_WARN("Failed to create state manager");
            return;
        }
        FF_LOG("State manager created successfully");
    }
    catch (const std::exception& e) {
        FF_WARN("Exception creating state manager: %s", e.what());
        return;
    }
    
    try {
        // Get vtable with error checking
        if (IsBadReadPtr(device, sizeof(void*))) {
            FF_WARN("Invalid device pointer");
            return;
        }

        void** vftable = *reinterpret_cast<void***>(device);
        if (!vftable || IsBadReadPtr(vftable, sizeof(void*) * 83)) {
            FF_WARN("Invalid vtable pointer");
            return;
        }
        
        FF_LOG("Got vtable: %p", vftable);

        // Get DrawIndexedPrimitive function address
        void* drawFunc = vftable[82];
        if (!drawFunc || IsBadCodePtr(reinterpret_cast<FARPROC>(drawFunc))) {
            FF_WARN("Invalid DrawIndexedPrimitive function pointer");
            return;
        }

        FF_LOG("Original DrawIndexedPrimitive address: %p", drawFunc);

        // Verify our detour function
        if (IsBadCodePtr(reinterpret_cast<FARPROC>(DrawIndexedPrimitive_Detour))) {
            FF_WARN("Invalid detour function pointer");
            return;
        }

        FF_LOG("Detour function address: %p", static_cast<void*>(DrawIndexedPrimitive_Detour));

        // Create hook
        try {
            Detouring::Hook::Target target(drawFunc);
            FF_LOG("Created hook target");

            s_drawHook.Create(
                target,
                reinterpret_cast<void*>(DrawIndexedPrimitive_Detour)
            );
            FF_LOG("Created hook");

            // Get and verify trampoline
            s_originalDrawIndexedPrimitive = s_drawHook.GetTrampoline<DrawIndexedPrimitive_t>();
            if (!s_originalDrawIndexedPrimitive) {
                FF_WARN("Failed to get trampoline function");
                return;
            }

            FF_LOG("Got trampoline: %p", reinterpret_cast<void*>(s_originalDrawIndexedPrimitive));

            // Enable hook
            if (!s_drawHook.Enable()) {
                FF_WARN("Failed to enable hook");
                return;
            }

            FF_LOG("Hook enabled successfully");

            // Verify hook is installed
            void** newVTable = *reinterpret_cast<void***>(device);
            void* newDrawFunc = newVTable[82];
            FF_LOG("New DrawIndexedPrimitive address: %p", newDrawFunc);

            if (newDrawFunc == drawFunc) {
                FF_WARN("Hook installation verification failed - function not replaced");
                return;
            }
        }
        catch (const std::exception& e) {
            FF_WARN("Exception during hook creation: %s", e.what());
            return;
        }
        
        // Create state manager
        try {
            m_stateManager = std::make_unique<FixedFunctionState>();
            FF_LOG("State manager created");
        }
        catch (const std::exception& e) {
            FF_WARN("Failed to create state manager: %s", e.what());
            return;
        }
        
        FF_LOG("Successfully initialized fixed function renderer");
    }
    catch (const std::exception& e) {
        FF_WARN("Failed to initialize: %s", e.what());
        return;
    }
    catch (...) {
        FF_WARN("Unknown error during initialization");
        return;
    }
}

void FixedFunctionRenderer::Shutdown() {
    if (s_drawHook.IsEnabled()) {
        s_drawHook.Disable();
    }
    
    m_stateManager.reset();
    m_device = nullptr;
    s_originalDrawIndexedPrimitive = nullptr;
    
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
        FF_WARN("No state manager available - reinitializing...");
        try {
            m_stateManager = std::make_unique<FixedFunctionState>();
            if (!m_stateManager) {
                FF_WARN("Failed to create state manager during recovery");
                return false;
            }
            FF_LOG("State manager recreated successfully");
        }
        catch (const std::exception& e) {
            FF_WARN("Exception creating state manager during recovery: %s", e.what());
            return false;
        }
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
        if (m_stateManager) {
            m_stateManager->Restore(device);
        }
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
    static int recursionDepth = 0;
    if (recursionDepth > 0) {
        // Prevent recursive calls
        return s_originalDrawIndexedPrimitive(
            device, PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
    }
    
    recursionDepth++;
    auto& instance = Instance();
    HRESULT result = D3D_OK;

    try {
        // Ensure materials interface is available
        if (!materials || !g_pMaterialSystem) {
            FF_LOG("MaterialSystem not available, using original path");
            result = s_originalDrawIndexedPrimitive(
                device, PrimitiveType, BaseVertexIndex,
                MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
            recursionDepth--;
            return result;
        }

        static bool first_call = true;
        if (first_call) {
            FF_LOG("First draw call intercepted!");
            FF_LOG("Device: %p", device);
            FF_LOG("MaterialSystem: %p", materials);
            first_call = false;
        }

        if (!s_originalDrawIndexedPrimitive) {
            FF_WARN("No original function available!");
            recursionDepth--;
            return D3D_OK;
        }

        static float lastDebugTime = 0.0f;
        float currentTime = Plat_FloatTime();
        
        // Debug Print - Hook Interception
        if (currentTime - lastDebugTime > 5.0f) {
            IMaterial* currentMat = materials->GetRenderContext()->GetCurrentMaterial();
            FF_LOG("Draw stats for last 5 seconds:");
            FF_LOG("  Total draws: %d", instance.m_stats.totalDrawCalls);
            FF_LOG("  FF draws: %d", instance.m_stats.fixedFunctionDrawCalls);
            FF_LOG("  Current material: %s", currentMat ? currentMat->GetName() : "null");
            lastDebugTime = currentTime;
        }

        instance.m_stats.totalDrawCalls++;

        // If fixed function is disabled, use original path
        if (!instance.m_enabled) {
            FF_LOG("Fixed Function disabled, using original path");
            result = s_originalDrawIndexedPrimitive(
                device, PrimitiveType, BaseVertexIndex,
                MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
            recursionDepth--;
            return result;
        }

        // Get current material
        IMaterial* material = materials->GetRenderContext()->GetCurrentMaterial();
        if (!material) {
            FF_LOG("No material found, using original path");
            result = s_originalDrawIndexedPrimitive(
                device, PrimitiveType, BaseVertexIndex,
                MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
            recursionDepth--;
            return result;
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
                recursionDepth--;
                return D3D_OK;
            }
            
            FF_WARN("Fixed function render failed, using original path");
        }

        // Update and log stats
        instance.m_stats.LogIfNeeded();

        FF_LOG("Using original render path");
        result = s_originalDrawIndexedPrimitive(
            device, PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
    }
    catch (const std::exception& e) {
        FF_WARN("Exception in DrawIndexedPrimitive_Detour: %s", e.what());
        result = s_originalDrawIndexedPrimitive(
            device, PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
    }
    catch (...) {
        FF_WARN("Unknown exception in DrawIndexedPrimitive_Detour");
        result = s_originalDrawIndexedPrimitive(
            device, PrimitiveType, BaseVertexIndex,
            MinVertexIndex, NumVertices, StartIndex, PrimitiveCount);
    }

    recursionDepth--;
    return result;
}