#include "render_mode_manager.h"
#include "render_state_logger.h"
#include <tier0/dbg.h>
#include "e_utils.h"

extern IMaterialSystem* materials;

HRESULT WINAPI RenderModeManager::SetFVF_Detour(IDirect3DDevice9* device, DWORD FVF) {
    static float s_lastDebugTime = 0.0f;
    float currentTime = GetTickCount64() / 1000.0f;

    // Log the FVF state
    RenderStateLogger::Instance().LogVertexFormat(FVF, "SetFVF");

    if (currentTime - s_lastDebugTime > 1.0f) {
        Msg("[RTX FVF] SetFVF called with FVF: 0x%x\n", FVF);
        s_lastDebugTime = currentTime;
    }

    auto& manager = RenderModeManager::Instance();
    
    if (manager.ShouldUseFVF()) {
        Msg("[RTX FVF] Using FVF mode: 0x%x\n", FVF);
        return manager.m_originalSetFVF(device, FVF);
    } else {
        auto decl = manager.CreateFVFDeclaration(FVF);
        if (decl) {
            return manager.m_originalSetVertexDeclaration(device, decl);
        }
    }
    
    return manager.m_originalSetFVF(device, FVF);
}

// Add DrawPrimitive hook
HRESULT WINAPI RenderModeManager::DrawPrimitive_Detour(IDirect3DDevice9* device,
    D3DPRIMITIVETYPE PrimitiveType,
    UINT StartVertex,
    UINT PrimitiveCount) {
    
    auto& manager = RenderModeManager::Instance();
    
    // Log the draw call
    RenderStateLogger::Instance().LogDrawCall(
        device, 
        PrimitiveType,
        StartVertex,
        PrimitiveCount,
        "DrawPrimitive");

    return manager.m_originalDrawPrimitive(device, PrimitiveType, StartVertex, PrimitiveCount);
}

HRESULT WINAPI RenderModeManager::DrawIndexedPrimitive_Detour(IDirect3DDevice9* device,
    D3DPRIMITIVETYPE PrimitiveType,
    INT BaseVertexIndex,
    UINT MinVertexIndex,
    UINT NumVertices,
    UINT StartIndex,
    UINT PrimitiveCount) {
    
    auto& manager = RenderModeManager::Instance();
    
    // Log the draw call
    RenderStateLogger::Instance().LogIndexedDrawCall(
        device,
        PrimitiveType,
        BaseVertexIndex,
        MinVertexIndex,
        NumVertices,
        StartIndex,
        PrimitiveCount,
        "DrawIndexedPrimitive");

    return manager.m_originalDrawIndexedPrimitive(
        device, PrimitiveType, BaseVertexIndex, MinVertexIndex,
        NumVertices, StartIndex, PrimitiveCount);
}

HRESULT WINAPI RenderModeManager::SetVertexDeclaration_Detour(IDirect3DDevice9* device, IDirect3DVertexDeclaration9* decl) {
    auto& manager = RenderModeManager::Instance();
    
    if (manager.ShouldUseFVF()) {
        UINT numElements = 0;
        if (decl && SUCCEEDED(decl->GetDeclaration(nullptr, &numElements))) {
            std::vector<D3DVERTEXELEMENT9> elements(numElements);
            DWORD fvf = 0;
            if (SUCCEEDED(decl->GetDeclaration(elements.data(), &numElements))) {
                fvf = D3DFVF_XYZ;  // At minimum has position
                return manager.m_originalSetFVF(device, fvf);
            }
        }
    }
    
    return manager.m_originalSetVertexDeclaration(device, decl);
}

HRESULT WINAPI RenderModeManager::SetStreamSource_Detour(IDirect3DDevice9* device, 
    UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData,
    UINT OffsetInBytes, UINT Stride) {
    
    auto& manager = RenderModeManager::Instance();
    
    if (!manager.m_originalSetStreamSource) {
        return D3DERR_INVALIDCALL;
    }
    
    if (manager.ShouldUseFVF() && pStreamData) {
        DWORD currentFVF;
        device->GetFVF(&currentFVF);
        if (currentFVF != 0) {
            UINT fvfStride = manager.GetFVFStride(currentFVF);
            if (fvfStride != 0) {
                Stride = fvfStride;
            }
        }
    }
    
    return manager.m_originalSetStreamSource(device, StreamNumber, 
        pStreamData, OffsetInBytes, Stride);
}

void RenderStateLogger::LogVertexFormat(DWORD fvf, const char* context) {
    if (!ShouldLog()) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    LogMessage("Vertex Format (from %s): %s\n", context, FormatFVF(fvf).c_str());
}

RenderModeManager& RenderModeManager::Instance() {
    static RenderModeManager instance;
    return instance;
}

RenderModeManager::RenderModeManager() 
    : m_worldFVFEnabled(false)
    , m_modelsFVFEnabled(false)
    , m_initialized(false)
    , m_device(nullptr)
    , m_originalSetFVF(nullptr)
    , m_originalSetVertexDeclaration(nullptr)
    , m_originalSetStreamSource(nullptr)
    , m_originalDrawPrimitive(nullptr)
    , m_originalDrawIndexedPrimitive(nullptr) {
    InitializeCriticalSection(&m_cs);
}

RenderModeManager::~RenderModeManager() {
    Shutdown();
    DeleteCriticalSection(&m_cs);
}

// Add validation method:
bool RenderModeManager::ValidateVertexBuffer(IDirect3DVertexBuffer9* buffer, DWORD fvf) {
    if (!buffer) {
        LogMessage("Null vertex buffer passed to validation\n");
        return false;
    }

    EnterCriticalSection(&m_cs);
    auto& info = m_vertexBufferCache[buffer];
    
    if (info.fvf != fvf) {
        D3DVERTEXBUFFER_DESC desc;
        if (SUCCEEDED(buffer->GetDesc(&desc))) {
            info.fvf = fvf;
            info.stride = GetFVFStride(fvf);
            info.isModel = IsModelDrawing();
            info.isWorld = IsWorldDrawing();
            LogMessage("Updated vertex buffer info - FVF: %08X, Stride: %u\n", fvf, info.stride);
        } else {
            LogMessage("Failed to get vertex buffer description\n");
            LeaveCriticalSection(&m_cs);
            return false;
        }
    }
    
    LeaveCriticalSection(&m_cs);
    return true;
}

UINT RenderModeManager::GetFVFStride(DWORD fvf) {
    UINT stride = 0;
    
    if (fvf & D3DFVF_XYZ) stride += sizeof(float) * 3;
    if (fvf & D3DFVF_NORMAL) stride += sizeof(float) * 3;
    if (fvf & D3DFVF_DIFFUSE) stride += sizeof(DWORD);
    if (fvf & D3DFVF_SPECULAR) stride += sizeof(DWORD);
    
    DWORD numTexCoords = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    stride += numTexCoords * (sizeof(float) * 2);
    
    return stride;
}

void RenderModeManager::ClearVertexBufferCache() {
    EnterCriticalSection(&m_cs);
    m_vertexBufferCache.clear();
    LeaveCriticalSection(&m_cs);
}

void RenderModeManager::RestoreState() {
    if (!m_device || !m_initialized) return;
    
    EnterCriticalSection(&m_cs);
    
    // Reset to default FVF state
    if (m_originalSetFVF) {
        m_originalSetFVF(m_device, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
    }
    
    // Clear any active vertex declaration
    if (m_originalSetVertexDeclaration) {
        m_originalSetVertexDeclaration(m_device, nullptr);
    }
    
    LeaveCriticalSection(&m_cs);
}

void RenderModeManager::Initialize(IDirect3DDevice9* device) {
    EnterCriticalSection(&m_cs);
    
    try {
        if (!device) {
            LogMessage("Invalid device pointer\n");
            LeaveCriticalSection(&m_cs);
            return;
        }

        m_device = device;
        Msg("[RTX FVF] Initializing with device: %p\n", device);

        // Get D3D9 vtable
        void** vTable = *reinterpret_cast<void***>(device);
        Msg("[RTX FVF] Device vtable: %p\n", vTable);
        
        try {
            // Hook SetFVF (index 89)
            Msg("[RTX FVF] Setting up SetFVF hook at index 89: %p\n", vTable[89]);
            auto setFVFHook = new Detouring::Hook();
            Detouring::Hook::Target fvfTarget(&vTable[89]);
            setFVFHook->Create(fvfTarget, SetFVF_Detour);
            m_originalSetFVF = setFVFHook->GetTrampoline<SetFVF_t>();
            setFVFHook->Enable();
            m_hooks.push_back(setFVFHook);
            
            // Hook SetVertexDeclaration (index 87)
            Msg("[RTX FVF] Setting up SetVertexDeclaration hook at index 87: %p\n", vTable[87]);
            auto setDeclHook = new Detouring::Hook();
            Detouring::Hook::Target declTarget(&vTable[87]);
            setDeclHook->Create(declTarget, SetVertexDeclaration_Detour);
            m_originalSetVertexDeclaration = setDeclHook->GetTrampoline<SetVertexDeclaration_t>();
            setDeclHook->Enable();
            m_hooks.push_back(setDeclHook);

            // Hook SetStreamSource (index 100)
            Msg("[RTX FVF] Setting up SetStreamSource hook at index 100: %p\n", vTable[100]);
            auto setStreamHook = new Detouring::Hook();
            Detouring::Hook::Target streamTarget(&vTable[100]);
            setStreamHook->Create(streamTarget, SetStreamSource_Detour);
            m_originalSetStreamSource = setStreamHook->GetTrampoline<SetStreamSource_t>();
            setStreamHook->Enable();
            m_hooks.push_back(setStreamHook);

            // Hook DrawPrimitive (index 81)
            Msg("[RTX FVF] Setting up DrawPrimitive hook at index 81: %p\n", vTable[81]);
            auto drawPrimHook = new Detouring::Hook();
            Detouring::Hook::Target drawPrimTarget(&vTable[81]);
            drawPrimHook->Create(drawPrimTarget, DrawPrimitive_Detour);
            m_originalDrawPrimitive = drawPrimHook->GetTrampoline<DrawPrimitive_t>();
            drawPrimHook->Enable();
            m_hooks.push_back(drawPrimHook);

            // Hook DrawIndexedPrimitive (index 82)
            Msg("[RTX FVF] Setting up DrawIndexedPrimitive hook at index 82: %p\n", vTable[82]);
            auto drawIdxPrimHook = new Detouring::Hook();
            Detouring::Hook::Target drawIdxPrimTarget(&vTable[82]);
            drawIdxPrimHook->Create(drawIdxPrimTarget, DrawIndexedPrimitive_Detour);
            m_originalDrawIndexedPrimitive = drawIdxPrimHook->GetTrampoline<DrawIndexedPrimitive_t>();
            drawIdxPrimHook->Enable();
            m_hooks.push_back(drawIdxPrimHook);

        }
        catch (const std::exception& e) {
            LogMessage("Exception while creating hooks: %s\n", e.what());
            throw;
        }

        m_initialized = true;
        LogMessage("Render mode manager initialized successfully\n");
    }
    catch (const std::exception& e) {
        LogMessage("Exception during initialization: %s\n", e.what());
        // Cleanup any hooks that were created
        for (auto hook : m_hooks) {
            delete hook;
        }
        m_hooks.clear();
    }
    catch (...) {
        LogMessage("Unknown exception during initialization\n");
        // Cleanup any hooks that were created
        for (auto hook : m_hooks) {
            delete hook;
        }
        m_hooks.clear();
    }
    
    LeaveCriticalSection(&m_cs);
}

void RenderModeManager::Shutdown() {
    EnterCriticalSection(&m_cs);
    
    // Restore default state before shutting down
    RestoreState();
    
    // Cleanup hooks
    for (auto* hook : m_hooks) {
        if (hook) {
            hook->Disable();
            delete hook;
        }
    }
    m_hooks.clear();
    
    ClearFVFCache();
    ClearVertexBufferCache();
    
    // Clear function pointers
    m_originalSetFVF = nullptr;
    m_originalSetVertexDeclaration = nullptr;
    m_originalSetStreamSource = nullptr;
    
    m_initialized = false;
    m_device = nullptr;
    
    LogMessage("Shutdown complete\n");
    
    LeaveCriticalSection(&m_cs);
}

void RenderModeManager::EnableFVFForWorld(bool enable) {
    EnterCriticalSection(&m_cs);
    m_worldFVFEnabled = enable;
    Msg("[RTX FVF] World FVF %s\n", enable ? "enabled" : "disabled");
    LeaveCriticalSection(&m_cs);
}

void RenderModeManager::EnableFVFForModels(bool enable) {
    EnterCriticalSection(&m_cs);
    m_modelsFVFEnabled = enable;
    Msg("[RTX FVF] Model FVF %s\n", enable ? "enabled" : "disabled");
    LeaveCriticalSection(&m_cs);
}

bool RenderModeManager::ShouldUseFVF() const {
    if (!m_initialized) {
        Msg("[RTX FVF] Manager not initialized\n");
        return false;
    }
    
    bool isWorld = IsWorldDrawing();
    bool isModel = IsModelDrawing();
    
    static float lastDebugTime = 0.0f;
    float currentTime = GetTickCount64() / 1000.0f;
    if (currentTime - lastDebugTime > 1.0f) {
        Msg("[RTX FVF] Draw type - World: %d, Model: %d, FVF enabled - World: %d, Model: %d\n",
            isWorld, isModel, m_worldFVFEnabled, m_modelsFVFEnabled);
        lastDebugTime = currentTime;
    }

    if (m_worldFVFEnabled && isWorld) return true;
    if (m_modelsFVFEnabled && isModel) return true;
    
    return false;
}

bool RenderModeManager::IsWorldDrawing() const {
    if (!materials) return false;
    
    IMatRenderContext* renderContext = materials->GetRenderContext();
    if (!renderContext) return false;

    IMaterial* currentMaterial = renderContext->GetCurrentMaterial();
    if (!currentMaterial) return false;

    const char* materialName = currentMaterial->GetName();
    const char* shaderName = currentMaterial->GetShaderName();

    static float lastDebugTime = 0.0f;
    float currentTime = GetTickCount64() / 1000.0f;
    if (currentTime - lastDebugTime > 1.0f) {
        Msg("[RTX FVF] World Check - Material: %s, Shader: %s\n", 
            materialName ? materialName : "null",
            shaderName ? shaderName : "null");
        lastDebugTime = currentTime;
    }

    // Expanded world material patterns
    return (materialName && (
        strstr(materialName, "world") != nullptr ||
        strstr(materialName, "brush") != nullptr ||
        strstr(materialName, "displacement") != nullptr ||
        strstr(materialName, "concrete") != nullptr ||
        strstr(materialName, "brick") != nullptr ||
        strstr(materialName, "wall") != nullptr ||
        strstr(materialName, "tile") != nullptr
    ));
}

bool RenderModeManager::IsModelDrawing() const {
    if (!materials) return false;
    
    IMatRenderContext* renderContext = materials->GetRenderContext();
    if (!renderContext) return false;

    IMaterial* currentMaterial = renderContext->GetCurrentMaterial();
    if (!currentMaterial) return false;

    const char* materialName = currentMaterial->GetName();
    const char* shaderName = currentMaterial->GetShaderName();

    static float lastDebugTime = 0.0f;
    float currentTime = GetTickCount64() / 1000.0f;
    if (currentTime - lastDebugTime > 1.0f) {
        Msg("[RTX FVF] Model Check - Material: %s, Shader: %s\n", 
            materialName ? materialName : "null",
            shaderName ? shaderName : "null");
        lastDebugTime = currentTime;
    }

    return (
        (materialName && strstr(materialName, "model") != nullptr) ||
        (shaderName && (
            strstr(shaderName, "VertexLitGeneric") != nullptr ||
            strstr(shaderName, "LightmappedGeneric") != nullptr ||
            strstr(shaderName, "UnlitGeneric") != nullptr
        ))
    );
}

IDirect3DVertexDeclaration9* RenderModeManager::CreateFVFDeclaration(DWORD fvf) {
    if (!m_device) return nullptr;

    EnterCriticalSection(&m_cs);
    
    auto it = m_fvfCache.find(fvf);
    if (it != m_fvfCache.end()) {
        auto result = it->second.declaration;
        LeaveCriticalSection(&m_cs);
        return result;
    }

    // Convert FVF to vertex declaration elements
    std::vector<D3DVERTEXELEMENT9> elements;
    WORD offset = 0;

    // Position
    if (fvf & D3DFVF_XYZ) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0});
        offset += 12;
    }

    // Normal
    if (fvf & D3DFVF_NORMAL) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0});
        offset += 12;
    }

    // Diffuse color
    if (fvf & D3DFVF_DIFFUSE) {
        elements.push_back({0, offset, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0});
        offset += 4;
    }

    // Texture coordinates
    DWORD numTexCoords = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (DWORD i = 0; i < numTexCoords; i++) {
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, (BYTE)i});
        offset += 8;
    }

    // End marker
    elements.push_back(D3DDECL_END());

    // Create vertex declaration
    IDirect3DVertexDeclaration9* declaration;
    HRESULT hr = m_device->CreateVertexDeclaration(elements.data(), &declaration);
    if (SUCCEEDED(hr)) {
        FVFFormat format;
        format.fvf = fvf;
        format.declaration = declaration;
        m_fvfCache[fvf] = format;
        
        LeaveCriticalSection(&m_cs);
        return declaration;
    }

    LeaveCriticalSection(&m_cs);
    return nullptr;
}

void RenderModeManager::ClearFVFCache() {
    EnterCriticalSection(&m_cs);
    
    for (auto& pair : m_fvfCache) {
        if (pair.second.declaration) {
            pair.second.declaration->Release();
        }
    }
    m_fvfCache.clear();
    
    LeaveCriticalSection(&m_cs);
}

void RenderModeManager::LogMessage(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Msg("[Render Mode Manager] %s", buffer);
}