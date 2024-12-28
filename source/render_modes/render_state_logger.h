#pragma once
#include <d3d9.h>
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include "tier0/dbg.h"
#include "../shader_fixes/shader_hooks.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"

class RenderStateLogger {
public:
    static RenderStateLogger& Instance() {
        static RenderStateLogger instance;
        return instance;
    }

    void Initialize(IDirect3DDevice9* device);
    void Shutdown();

    // Logging entry points
    void LogDrawCall(IDirect3DDevice9* device, D3DPRIMITIVETYPE primType, 
        UINT startVertex, UINT primCount, const char* context = "Unknown");
    void LogIndexedDrawCall(IDirect3DDevice9* device, D3DPRIMITIVETYPE primType,
        INT baseVertexIndex, UINT minVertexIndex, UINT numVertices,
        UINT startIndex, UINT primCount, const char* context = "Unknown");
    void LogStateChange(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, 
        DWORD value, const char* context = "Unknown");
    void LogMaterialState(IMaterial* material, const char* context = "Unknown");
    void LogShaderState(IDirect3DVertexShader9* vshader, IDirect3DPixelShader9* pshader,
        const char* context = "Unknown");

    // Manual control
    void EnableLogging(bool enable) { m_loggingEnabled = enable; }
    void ForceDump() { DumpLogToFile(); }
    void ClearLog() { 
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logEntries.clear(); 
    }
    
    void SetLoggingInterval(float interval) { 
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logInterval = interval; 
    }
    void LogVertexFormat(DWORD fvf, const char* context = "Unknown");

private:
    RenderStateLogger() : m_device(nullptr), m_initialized(false), 
        m_loggingEnabled(false), m_lastLogTime(0.0f) {}
    ~RenderStateLogger() { Shutdown(); }

    struct DrawCallInfo {
        D3DPRIMITIVETYPE primType;
        UINT vertexCount;
        UINT primitiveCount;
        DWORD fvf;
        DWORD vertexFormat;
        bool indexed;
        bool hasVertexShader;
        bool hasPixelShader;
        std::string materialName;
        std::string shaderName;
        D3DMATRIX worldMatrix;
        DWORD renderStates[32];  // Store key render states
    };

    struct LogEntry {
        float time;
        std::string context;
        std::string callStack;
        DrawCallInfo drawInfo;
    };

    IDirect3DDevice9* m_device;
    std::vector<LogEntry> m_logEntries;
    std::mutex m_mutex;
    bool m_initialized;
    bool m_loggingEnabled;
    float m_lastLogTime;
    static constexpr float LOG_INTERVAL = 0.016f;  // ~60fps

    void DumpLogToFile();
    std::string GetCallStack();
    std::string FormatDrawCallInfo(const DrawCallInfo& info);
    bool ShouldLog();
    void CaptureKeyRenderStates(IDirect3DDevice9* device, DWORD* states);
    std::string GetShaderDescription(IDirect3DVertexShader9* shader);
    void LogMessage(const char* format, ...);
    std::string FormatBlendMode(DWORD mode);
    std::string FormatCullMode(DWORD mode);
    std::string FormatFillMode(DWORD mode);
    std::string FormatFVF(DWORD fvf);
    float m_logInterval;
};

// Helper macro for context logging
#define LOG_RENDER_CONTEXT(device, fmt, ...) \
    do { \
        char context[256]; \
        snprintf(context, sizeof(context), fmt, ##__VA_ARGS__); \
        RenderStateLogger::Instance().LogDrawCall(device, D3DPT_TRIANGLELIST, 0, 0, context); \
    } while(0)