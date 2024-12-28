#include "render_mode_manager.h"
#include <tier0/dbg.h>
#include "e_utils.h"

extern IMaterialSystem* materials;

HRESULT WINAPI SetFVF_Detour(IDirect3DDevice9* device, DWORD FVF) {
    auto& manager = RenderModeManager::Instance();
    
    if (manager.ShouldUseFVF()) {
        return manager.m_originalSetFVF(device, FVF);
    } else {
        auto decl = manager.CreateFVFDeclaration(FVF);
        if (decl) {
            return manager.m_originalSetVertexDeclaration(device, decl);
        }
    }
    
    return manager.m_originalSetFVF(device, FVF);
}

HRESULT WINAPI SetVertexDeclaration_Detour(IDirect3DDevice9* device, IDirect3DVertexDeclaration9* decl) {
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

HRESULT WINAPI SetStreamSource_Detour(IDirect3DDevice9* device, 
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
    , m_originalSetVertexDeclaration(nullptr) {
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

        // Get D3D9 vtable
        void** vTable = *reinterpret_cast<void***>(device);
        
        // Hook SetFVF (index 89)
        Detouring::Hook::Target fvfTarget(&vTable[89]);
        auto setFVFHook = new Detouring::Hook();
        setFVFHook->Create(fvfTarget, &SetFVF_Detour);
        m_originalSetFVF = setFVFHook->GetTrampoline<SetFVF_t>();
        
        // Hook SetVertexDeclaration (index 87)
        Detouring::Hook::Target declTarget(&vTable[87]);
        auto setDeclHook = new Detouring::Hook();
        setDeclHook->Create(declTarget, &SetVertexDeclaration_Detour);
        m_originalSetVertexDeclaration = setDeclHook->GetTrampoline<SetVertexDeclaration_t>();

        // Hook SetStreamSource (index 100)
        Detouring::Hook::Target streamTarget(&vTable[100]);
        auto setStreamHook = new Detouring::Hook();
        setStreamHook->Create(streamTarget, &SetStreamSource_Detour);
        m_originalSetStreamSource = setStreamHook->GetTrampoline<SetStreamSource_t>();

        m_initialized = true;
        LogMessage("Render mode manager initialized\n");
    }
    catch (...) {
        LogMessage("Exception during initialization\n");
    }
    
    LeaveCriticalSection(&m_cs);
}

void RenderModeManager::Shutdown() {
    EnterCriticalSection(&m_cs);
    
    // Restore default state before shutting down
    RestoreState();
    
    ClearFVFCache();
    ClearVertexBufferCache();
    
    // Clear function pointers
    m_originalSetFVF = nullptr;
    m_originalSetVertexDeclaration = nullptr;
    m_originalSetStreamSource = nullptr;
    
    m_initialized = false;
    m_device = nullptr;
    
    LeaveCriticalSection(&m_cs);
}

void RenderModeManager::EnableFVFForWorld(bool enable) {
    EnterCriticalSection(&m_cs);
    m_worldFVFEnabled = enable;
    LeaveCriticalSection(&m_cs);
}

void RenderModeManager::EnableFVFForModels(bool enable) {
    EnterCriticalSection(&m_cs);
    m_modelsFVFEnabled = enable;
    LeaveCriticalSection(&m_cs);
}

bool RenderModeManager::ShouldUseFVF() const {
    if (!m_initialized) return false;
    
    if (m_worldFVFEnabled && IsWorldDrawing()) return true;
    if (m_modelsFVFEnabled && IsModelDrawing()) return true;
    
    return false;
}

bool RenderModeManager::IsWorldDrawing() const {
    if (!materials) return false;
    
    IMatRenderContext* renderContext = materials->GetRenderContext();
    if (!renderContext) return false;

    IMaterial* currentMaterial = renderContext->GetCurrentMaterial();
    if (!currentMaterial) return false;

    // Check material/shader names used by world geometry
    const char* materialName = currentMaterial->GetName();
    const char* shaderName = currentMaterial->GetShaderName();

    // Common world material patterns
    return (strstr(materialName, "world") != nullptr ||
            strstr(materialName, "brush") != nullptr ||
            strstr(materialName, "displacement") != nullptr ||
            // Add other world material patterns here
            false);
}

bool RenderModeManager::IsModelDrawing() const {
    if (!materials) return false;
    
    IMatRenderContext* renderContext = materials->GetRenderContext();
    if (!renderContext) return false;

    IMaterial* currentMaterial = renderContext->GetCurrentMaterial();
    if (!currentMaterial) return false;

    // Check if we're in a model rendering pass
    const char* materialName = currentMaterial->GetName();
    const char* shaderName = currentMaterial->GetShaderName();

    return (strstr(materialName, "model") != nullptr ||
            strstr(shaderName, "VertexLitGeneric") != nullptr ||
            // Add other model material/shader patterns
            false);
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