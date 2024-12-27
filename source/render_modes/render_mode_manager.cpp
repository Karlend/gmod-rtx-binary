#include "render_mode_manager.h"
#include <tier0/dbg.h>
#include "e_utils.h"

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
        setFVFHook->Create(fvfTarget, [](IDirect3DDevice9* device, DWORD FVF) -> HRESULT {
            auto& manager = RenderModeManager::Instance();
            
            if (manager.ShouldUseFVF()) {
                // Use FVF for world/model rendering
                return manager.m_originalSetFVF(device, FVF);
            } else {
                // Use vertex declarations for everything else
                auto decl = manager.CreateFVFDeclaration(FVF);
                if (decl) {
                    return manager.m_originalSetVertexDeclaration(device, decl);
                }
            }
            
            return manager.m_originalSetFVF(device, FVF);
        });
        
        m_originalSetFVF = setFVFHook->GetTrampoline<SetFVF_t>();
        
        // Hook SetVertexDeclaration (index 87)
        Detouring::Hook::Target declTarget(&vTable[87]);
        auto setDeclHook = new Detouring::Hook();
        setDeclHook->Create(declTarget, [](IDirect3DDevice9* device, IDirect3DVertexDeclaration9* decl) -> HRESULT {
            auto& manager = RenderModeManager::Instance();
            
            if (manager.ShouldUseFVF()) {
                // Convert vertex declaration to FVF if possible
                DWORD fvf = 0;
                if (decl && SUCCEEDED(decl->GetDeclaration(nullptr, &fvf))) {
                    return manager.m_originalSetFVF(device, fvf);
                }
            }
            
            return manager.m_originalSetVertexDeclaration(device, decl);
        });
        
        m_originalSetVertexDeclaration = setDeclHook->GetTrampoline<SetVertexDeclaration_t>();

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
    
    ClearFVFCache();
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
    // Check call stack or rendering state to determine if world geometry is being drawn
    // This would need to be customized based on Source Engine's rendering patterns
    return false; // Placeholder
}

bool RenderModeManager::IsModelDrawing() const {
    // Similar to IsWorldDrawing, but for model rendering
    return false; // Placeholder
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