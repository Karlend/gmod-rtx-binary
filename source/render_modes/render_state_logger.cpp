#include "render_state_logger.h"
#include <sstream>
#include <iomanip>
#include <Windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#include "render_mode_manager.h" 

IDirect3DVertexDeclaration9* decl = nullptr;


void RenderStateLogger::Initialize(IDirect3DDevice9* device) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) {
        Msg("[Render Logger] Already initialized\n");
        return;
    }

    if (!device) {
        Msg("[Render Logger] Null device in Initialize\n");
        return;
    }

    m_device = device;
    m_initialized = true;
    m_loggingEnabled = false;  // Start disabled
    m_lastLogTime = 0.0f;
    m_logInterval = 0.016f;
    m_logEntries.reserve(1000);

    Msg("[Render Logger] Successfully initialized with device %p\n", device);
}

void RenderStateLogger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) return;

    DumpLogToFile();
    m_logEntries.clear();
    m_initialized = false;
    m_device = nullptr;

    LogMessage("Render logger shut down\n");
}

// Add these helper functions
std::string RenderStateLogger::FormatBlendMode(DWORD mode) {
    switch (mode) {
        case D3DBLEND_ZERO: return "ZERO";
        case D3DBLEND_ONE: return "ONE";
        case D3DBLEND_SRCCOLOR: return "SRCCOLOR";
        case D3DBLEND_INVSRCCOLOR: return "INVSRCCOLOR";
        case D3DBLEND_SRCALPHA: return "SRCALPHA";
        case D3DBLEND_INVSRCALPHA: return "INVSRCALPHA";
        case D3DBLEND_DESTALPHA: return "DESTALPHA";
        case D3DBLEND_INVDESTALPHA: return "INVDESTALPHA";
        case D3DBLEND_DESTCOLOR: return "DESTCOLOR";
        case D3DBLEND_INVDESTCOLOR: return "INVDESTCOLOR";
        default: return "UNKNOWN";
    }
}

std::string RenderStateLogger::FormatCullMode(DWORD mode) {
    switch (mode) {
        case D3DCULL_NONE: return "NONE";
        case D3DCULL_CW: return "CW";
        case D3DCULL_CCW: return "CCW";
        default: return "UNKNOWN";
    }
}

std::string RenderStateLogger::FormatFillMode(DWORD mode) {
    switch (mode) {
        case D3DFILL_POINT: return "POINT";
        case D3DFILL_WIREFRAME: return "WIREFRAME";
        case D3DFILL_SOLID: return "SOLID";
        default: return "UNKNOWN";
    }
}

std::string RenderStateLogger::FormatFVF(DWORD fvf) {
    std::vector<std::string> flags;

    if (fvf & D3DFVF_XYZ) flags.push_back("XYZ");
    if (fvf & D3DFVF_NORMAL) flags.push_back("NORMAL");
    if (fvf & D3DFVF_DIFFUSE) flags.push_back("DIFFUSE");
    if (fvf & D3DFVF_SPECULAR) flags.push_back("SPECULAR");

    DWORD numTexCoords = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (numTexCoords > 0) {
        flags.push_back("TEX" + std::to_string(numTexCoords));
    }

    std::stringstream ss;
    for (size_t i = 0; i < flags.size(); i++) {
        if (i > 0) ss << "|";
        ss << flags[i];
    }

    return ss.str();
}

void RenderStateLogger::DumpLogToFile() {
    static int fileIndex = 0;
    char filename[256];
    sprintf_s(filename, "garrysmod/data/rtx_render_log_%d.txt", fileIndex++);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        LogMessage("Failed to open log file: %s\n", filename);
        return;
    }

    file << "=== RTX Render State Log ===\n\n";
    file << "Total Entries: " << m_logEntries.size() << "\n";
    file << "Capture Time: " << GetTickCount64() / 1000.0f << " seconds\n\n";
    
    for (const auto& entry : m_logEntries) {
        file << "\n=== Entry at " << std::fixed << std::setprecision(3) << entry.time << "s ===\n";
        file << "Context: " << entry.context << "\n\n";
        
        if (!entry.callStack.empty()) {
            file << "Call Stack:\n" << entry.callStack << "\n";
        }
        
        file << FormatDrawCallInfo(entry.drawInfo) << "\n";
        file << "------------------------------------------------\n";
    }
    
    file.close();
    LogMessage("Dumped %zu entries to %s\n", m_logEntries.size(), filename);
}

void RenderStateLogger::LogDrawCall(IDirect3DDevice9* device, D3DPRIMITIVETYPE primType,
    UINT startVertex, UINT primCount, const char* context) {
    
    static float s_lastDebugTime = 0.0f;
    float currentTime = GetTickCount64() / 1000.0f;

    Msg("[Render Logger] LogDrawCall - Context: %s, Prims: %d\n", 
        context ? context : "Unknown", primCount);

    if (!ShouldLog()) {
        if (currentTime - s_lastDebugTime > 1.0f) {
            Msg("[Render Logger] Skipping log - Initialized: %d, Enabled: %d, Device: %p\n",
                m_initialized, m_loggingEnabled, m_device);
            s_lastDebugTime = currentTime;
        }
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    
    LogEntry entry;
    entry.time = GetTickCount64() / 1000.0f;
    entry.context = context ? context : "Unknown";

    // Get current material info from Source engine
    if (materials) {
        IMatRenderContext* renderContext = materials->GetRenderContext();
        if (renderContext) {
            IMaterial* currentMaterial = renderContext->GetCurrentMaterial();
            if (currentMaterial) {
                entry.drawInfo.materialName = currentMaterial->GetName();
                entry.drawInfo.shaderName = currentMaterial->GetShaderName();
                
                // Log additional material properties
                IMaterialVar* baseTexture = currentMaterial->FindVar("$basetexture", nullptr);
                if (baseTexture) {
                    entry.drawInfo.materialName += " (Texture: " + std::string(baseTexture->GetStringValue()) + ")";
                }
            }
        }
    }

    // Get current FVF and shaders
    device->GetFVF(&entry.drawInfo.fvf);

    // Get shader state
    IDirect3DVertexShader9* vshader = nullptr;
    IDirect3DPixelShader9* pshader = nullptr;
    device->GetVertexShader(&vshader);
    device->GetPixelShader(&pshader);

    entry.drawInfo.hasVertexShader = vshader != nullptr;
    entry.drawInfo.hasPixelShader = pshader != nullptr;

    // Get shader details if available
    if (vshader) {
        entry.drawInfo.shaderName = GetShaderDescription(vshader);
        vshader->Release();
    }
    if (pshader) {
        pshader->Release();
    }

    // Capture render states
    CaptureKeyRenderStates(device, entry.drawInfo.renderStates);

    // Get transforms
    device->GetTransform(D3DTS_WORLD, &entry.drawInfo.worldMatrix);
    
    // Get vertex buffer info if available
    IDirect3DVertexBuffer9* vertexBuffer = nullptr;
    UINT offset = 0, stride = 0;
    if (SUCCEEDED(device->GetStreamSource(0, &vertexBuffer, &offset, &stride))) {
        if (vertexBuffer) {
            D3DVERTEXBUFFER_DESC desc;
            if (SUCCEEDED(vertexBuffer->GetDesc(&desc))) {
                entry.drawInfo.vertexCount = desc.Size / (stride ? stride : 1);
                entry.drawInfo.vertexFormat = desc.FVF;
            }
            vertexBuffer->Release();
        }
    }

    entry.drawInfo.primType = primType;
    entry.drawInfo.primitiveCount = primCount;
    entry.drawInfo.indexed = false;

    // Get call stack
    entry.callStack = GetCallStack();

    m_logEntries.push_back(entry);

    LogMessage("Captured draw call - Material: %s, Shader: %s, Prims: %d\n",
        entry.drawInfo.materialName.c_str(),
        entry.drawInfo.shaderName.c_str(),
        primCount);

    // Dump if we have enough entries
    if (m_logEntries.size() >= 1000) {
        DumpLogToFile();
        m_logEntries.clear();
    }
}

void RenderStateLogger::LogIndexedDrawCall(IDirect3DDevice9* device, D3DPRIMITIVETYPE primType,
    INT baseVertexIndex, UINT minVertexIndex, UINT numVertices,
    UINT startIndex, UINT primCount, const char* context) {
    
    if (!ShouldLog()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    
    LogEntry entry;
    entry.time = GetTickCount64() / 1000.0f;
    entry.context = context;
    entry.callStack = GetCallStack();

    DrawCallInfo& drawInfo = entry.drawInfo;
    drawInfo.primType = primType;
    drawInfo.vertexCount = numVertices;
    drawInfo.primitiveCount = primCount;
    drawInfo.indexed = true;

    device->GetFVF(&drawInfo.fvf);
    CaptureKeyRenderStates(device, drawInfo.renderStates);
    device->GetTransform(D3DTS_WORLD, &drawInfo.worldMatrix);

    // Get shader state
    IDirect3DVertexShader9* vshader;
    IDirect3DPixelShader9* pshader;
    device->GetVertexShader(&vshader);
    device->GetPixelShader(&pshader);
    
    drawInfo.hasVertexShader = vshader != nullptr;
    drawInfo.hasPixelShader = pshader != nullptr;

    if (vshader) {
        drawInfo.shaderName = GetShaderDescription(vshader);
        vshader->Release();
    }
    if (pshader) pshader->Release();

    m_logEntries.push_back(entry);
}

void RenderStateLogger::CaptureKeyRenderStates(IDirect3DDevice9* device, DWORD* states) {
    static const D3DRENDERSTATETYPE keyStates[] = {
        D3DRS_ZENABLE,
        D3DRS_ZWRITEENABLE,
        D3DRS_LIGHTING,
        D3DRS_ALPHABLENDENABLE,
        D3DRS_SRCBLEND,
        D3DRS_DESTBLEND,
        D3DRS_CULLMODE,
        D3DRS_FILLMODE
    };

    for (int i = 0; i < ARRAYSIZE(keyStates); i++) {
        device->GetRenderState(keyStates[i], &states[i]);
    }
}

std::string RenderStateLogger::GetCallStack() {
    void* stack[32];
    WORD frames = CaptureStackBackTrace(0, 32, stack, nullptr);
    
    std::stringstream ss;
    
    for (WORD i = 0; i < frames; i++) {
        DWORD64 address = (DWORD64)(stack[i]);
        DWORD64 displacement = 0;

        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;

        if (SymFromAddr(GetCurrentProcess(), address, &displacement, pSymbol)) {
            // Filter out common but uninteresting functions
            if (strstr(pSymbol->Name, "RenderStateLogger") ||
                strstr(pSymbol->Name, "LogDrawCall") ||
                strstr(pSymbol->Name, "CaptureStack"))
                continue;

            ss << pSymbol->Name << " <- ";
        }
    }

    return ss.str();
}

std::string RenderStateLogger::GetShaderDescription(IDirect3DVertexShader9* shader) {
    if (!shader) return "null";

    UINT size = 0;
    if (FAILED(shader->GetFunction(nullptr, &size)) || size == 0)
        return "invalid";

    std::vector<BYTE> shaderCode(size);
    if (FAILED(shader->GetFunction(shaderCode.data(), &size)))
        return "failed";

    // Get shader version
    DWORD version = *(DWORD*)shaderCode.data();
    std::stringstream ss;
    ss << "vs_" << D3DSHADER_VERSION_MAJOR(version) << "_" 
       << D3DSHADER_VERSION_MINOR(version);

    return ss.str();
}

std::string RenderStateLogger::FormatDrawCallInfo(const DrawCallInfo& info) {
    std::stringstream ss;

    ss << "Draw Call Info:\n"
       << "  Material: " << info.materialName << "\n"
       << "  Shader: " << info.shaderName << "\n"
       << "  Type: " << (info.indexed ? "Indexed" : "Non-indexed") << "\n"
       << "  Primitives: " << info.primitiveCount << "\n"
       << "  Vertices: " << info.vertexCount << "\n"
       << "  FVF: 0x" << std::hex << info.fvf << std::dec << " (" << FormatFVF(info.fvf) << ")\n"
       << "  Has Vertex Shader: " << (info.hasVertexShader ? "Yes" : "No") << "\n"
       << "  Has Pixel Shader: " << (info.hasPixelShader ? "Yes" : "No") << "\n"
       << "  Render States:\n"
       << "    Z-Enable: " << (info.renderStates[0] == D3DZB_TRUE ? "Yes" : "No") << "\n"
       << "    Z-Write: " << (info.renderStates[1] == TRUE ? "Yes" : "No") << "\n"
       << "    Lighting: " << (info.renderStates[2] == TRUE ? "Yes" : "No") << "\n"
       << "    Alpha Blend: " << (info.renderStates[3] == TRUE ? "Yes" : "No") << "\n"
       << "    Src Blend: " << FormatBlendMode(info.renderStates[4]) << "\n"
       << "    Dest Blend: " << FormatBlendMode(info.renderStates[5]) << "\n"
       << "    Cull Mode: " << FormatCullMode(info.renderStates[6]) << "\n"
       << "    Fill Mode: " << FormatFillMode(info.renderStates[7]) << "\n";

    return ss.str();
}

bool RenderStateLogger::ShouldLog() {
    if (!m_initialized || !m_loggingEnabled || !m_device) {
        static float s_lastErrorTime = 0.0f;
        float currentTime = GetTickCount64() / 1000.0f;
        if (currentTime - s_lastErrorTime > 1.0f) {
            Msg("[Render Logger] Cannot log - Initialized: %d, Enabled: %d, Device: %p\n",
                m_initialized, m_loggingEnabled, m_device);
            s_lastErrorTime = currentTime;
        }
        return false;
    }

    float currentTime = GetTickCount64() / 1000.0f;
    if (currentTime - m_lastLogTime < m_logInterval) {
        return false;
    }

    m_lastLogTime = currentTime;
    return true;
}

void RenderStateLogger::LogMessage(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Msg("[Render Logger] %s", buffer);
}