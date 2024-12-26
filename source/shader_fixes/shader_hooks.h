#pragma once
#include <map>
#include "../e_utils.h"
#include <tier0/dbg.h>
#include <materialsystem/imaterialsystem.h>
#include <materialsystem/imaterial.h>
#include <materialsystem/imaterialvar.h>
#include <shaderapi/ishaderapi.h>
#include <Windows.h>
#include <d3d9.h>
#include <unordered_set>
#include <string>
#include <regex>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#include <tier1/KeyValues.h>
#include <unordered_map>
#include <fstream>
#include <mutex>
#include <time.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

// Forward declare the shader API interface
class IShaderAPI;

extern IShaderAPI* g_pShaderAPI;
extern IDirect3DDevice9* g_pD3DDevice;

class ShaderAPIHooks {
public:
    static ShaderAPIHooks& Instance() {
        static ShaderAPIHooks instance;
        return instance;
    }

    void Initialize();
    void Shutdown();

private:
    ShaderAPIHooks() = default;
    ~ShaderAPIHooks() = default;

    static std::ofstream s_logFile;
    static bool s_loggingInitialized;
    static std::mutex s_logMutex;
    static std::string s_logPath;
    
    // Make InitializeLogging static
    static bool InitializeLogging();

    // Make LogToFile static (it was already marked static)
    static void LogToFile(const char* format, ...);

    // Tracking structure for shader state
    struct ShaderState {
        std::string lastMaterialName;
        std::string lastShaderName;
        std::string lastErrorMessage;
        float lastErrorTime = 0.0f;
        bool isProcessingParticle = false;
    };

    // Static members for state tracking
    static ShaderState s_state;
    static std::unordered_set<std::string> s_knownProblematicShaders;
    static std::unordered_set<std::string> s_problematicMaterials;
    static std::unordered_set<uintptr_t> s_problematicAddresses;
    static std::map<uint64_t, uint32_t> s_sequenceStarts;
    static bool s_inOcclusionProxy;

    // Hook objects
    static Detouring::Hook s_ConMsg_hook;
    Detouring::Hook m_DrawIndexedPrimitive_hook;
    Detouring::Hook m_SetVertexShaderConstantF_hook;
    Detouring::Hook m_SetStreamSource_hook;
    Detouring::Hook m_SetVertexShader_hook;
    Detouring::Hook m_VertexBufferLock_hook;
    Detouring::Hook m_DivisionFunction_hook;
    Detouring::Hook m_ParticleRender_hook;
    Detouring::Hook m_FindMaterial_hook;
    Detouring::Hook m_BeginRenderPass_hook;
    Detouring::Hook m_LoadMaterial_hook;
    Detouring::Hook m_CreateMaterial_hook;
    Detouring::Hook m_GetHardwareConfig_hook;
    static Detouring::Hook m_InitMaterialSystem_hook;
    static Detouring::Hook m_InitProxyMaterial_hook;

    // Function pointer types
    typedef void(__cdecl* ConMsg_t)(const char* fmt, ...);
    typedef HRESULT(__stdcall* DrawIndexedPrimitive_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
    typedef HRESULT(__stdcall* SetVertexShaderConstantF_t)(IDirect3DDevice9*, UINT, CONST float*, UINT);
    typedef HRESULT(__stdcall* SetStreamSource_t)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
    typedef HRESULT(__stdcall* SetVertexShader_t)(IDirect3DDevice9*, IDirect3DVertexShader9*);
    typedef HRESULT(__stdcall* VertexBufferLock_t)(void*, UINT, UINT, void**, DWORD);
    typedef int(__fastcall* DivisionFunction_t)(int a1, int a2, int dividend, int divisor);
    typedef void(__fastcall* ParticleRender_t)(void* thisptr);
    typedef IMaterial*(__fastcall* FindMaterial_t)(void* thisptr, void* edx, const char* materialName, const char* textureGroupName, bool complain, const char* complainPrefix);
    typedef void(__fastcall* BeginRenderPass_t)(IMatRenderContext* thisptr, void* edx, IMaterial* material);
    typedef IMaterial*(__thiscall* LoadMaterial_t)(void* thisptr, const char* materialName, const char* textureGroupName);
    typedef IMaterial*(__thiscall* CreateMaterial_t)(void* thisptr, const char* pMaterialName, KeyValues* pVMTKeyValues);
    typedef void*(__thiscall* GetHardwareConfig_t)(void* thisptr);
    typedef bool(__fastcall* InitMaterialSystem_t)(void* thisptr, void* edx, void* hardwareConfig, void* adapter, const char* materialBasedir);
    typedef void(__fastcall* InitProxyMaterial_t)(void* proxyData);

    // Original function pointers
    static ConMsg_t g_original_ConMsg;
    static DrawIndexedPrimitive_t g_original_DrawIndexedPrimitive;
    static SetVertexShaderConstantF_t g_original_SetVertexShaderConstantF;
    static SetStreamSource_t g_original_SetStreamSource;
    static SetVertexShader_t g_original_SetVertexShader;
    static VertexBufferLock_t g_original_VertexBufferLock;
    static DivisionFunction_t g_original_DivisionFunction;
    static ParticleRender_t g_original_ParticleRender;
    static FindMaterial_t g_original_FindMaterial;
    static BeginRenderPass_t g_original_BeginRenderPass;
    static LoadMaterial_t g_original_LoadMaterial;
    static CreateMaterial_t g_original_CreateMaterial;
    static GetHardwareConfig_t g_original_GetHardwareConfig;
    static InitMaterialSystem_t g_original_InitMaterialSystem;
    static InitProxyMaterial_t g_original_InitProxyMaterial;

    // Hook detour functions
    static void __cdecl ConMsg_detour(const char* fmt, ...);
    static HRESULT __stdcall DrawIndexedPrimitive_detour(IDirect3DDevice9* device, D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount);
    static HRESULT __stdcall SetVertexShaderConstantF_detour(IDirect3DDevice9* device, UINT StartRegister, CONST float* pConstantData, UINT Vector4fCount);
    static HRESULT __stdcall SetStreamSource_detour(IDirect3DDevice9* device, UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride);
    static HRESULT __stdcall SetVertexShader_detour(IDirect3DDevice9* device, IDirect3DVertexShader9* pShader);
    static HRESULT __stdcall VertexBufferLock_detour(void* thisptr, UINT offsetToLock, UINT sizeToLock, void** ppbData, DWORD flags);
    static int __fastcall DivisionFunction_detour(int a1, int a2, int dividend, int divisor);
    static void __fastcall ParticleRender_detour(void* thisptr);
    static IMaterial* __fastcall FindMaterial_detour(void* thisptr, void* edx, const char* materialName, const char* textureGroupName, bool complain, const char* complainPrefix);
    static void __fastcall BeginRenderPass_detour(IMatRenderContext* thisptr, void* edx, IMaterial* material);
    static IMaterial* __fastcall LoadMaterial_detour(void* thisptr, void* edx, const char* materialName, const char* textureGroupName);
    static IMaterial* __fastcall CreateMaterial_detour(void* thisptr, void* edx, const char* pMaterialName, KeyValues* pVMTKeyValues);
    static void* __fastcall GetHardwareConfig_detour(void* thisptr, void* edx);
    static bool __fastcall InitMaterialSystem_detour(void* thisptr, void* edx, void* hardwareConfig, void* adapter, const char* materialBasedir);
    static void __fastcall InitProxyMaterial_detour(void* proxyData);

    // Helper functions
    static bool ValidateVertexBuffer(IDirect3DVertexBuffer9* pVertexBuffer, UINT offsetInBytes, UINT stride);
    static bool ValidateParticleVertexBuffer(IDirect3DVertexBuffer9* pVertexBuffer, UINT stride);
    static bool ValidatePrimitiveParams(UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount);
    static bool ValidateVertexShader(IDirect3DVertexShader9* pShader);
    static bool ValidateShaderConstants(const float* pConstantData, UINT Vector4fCount, const char* shaderName);
    static bool IsParticleSystem();
    static bool IsOcclusionProxy(IMaterial* material);
    static bool IsKnownProblematicShader(const char* name);
    
    static void LogMessage(const char* format, ...);
    static void LogStackTrace(void* const* callStack, DWORD frameCount);
    static void LogShaderError(const char* format, ...);
    
    static void UpdateShaderState(const char* materialName, const char* shaderName);
    static void AddProblematicShader(const char* name);
    static void HandleOcclusionProxy();
    static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo);
    static void* FindPattern(const char* module, const char* pattern);
    
    // Trampoline helper functions
    static InitMaterialSystem_t InitMaterialSystem_trampoline() { 
        return m_InitMaterialSystem_hook.GetTrampoline<InitMaterialSystem_t>(); 
    }
    static InitProxyMaterial_t InitProxyMaterial_trampoline() { 
        return m_InitProxyMaterial_hook.GetTrampoline<InitProxyMaterial_t>(); 
    }
};

// Static member initializations
inline Detouring::Hook ShaderAPIHooks::m_InitMaterialSystem_hook;
inline Detouring::Hook ShaderAPIHooks::m_InitProxyMaterial_hook;
inline std::ofstream ShaderAPIHooks::s_logFile;
inline bool ShaderAPIHooks::s_loggingInitialized = false;
inline std::mutex ShaderAPIHooks::s_logMutex;
inline std::string ShaderAPIHooks::s_logPath;