#include "shader_hooks.h"
#include <algorithm>
#include <psapi.h>
#include <vector>
#include <cctype>
#pragma comment(lib, "psapi.lib")

// Global variables here
IShaderAPI* g_pShaderAPI = nullptr;
IDirect3DDevice9* g_pD3DDevice = nullptr;

// Initialize other static members
ShaderAPIHooks::ShaderState ShaderAPIHooks::s_state;
std::unordered_set<std::string> ShaderAPIHooks::s_knownProblematicShaders;
std::unordered_set<std::string> ShaderAPIHooks::s_problematicMaterials;
Detouring::Hook ShaderAPIHooks::s_ConMsg_hook;
ShaderAPIHooks::ConMsg_t ShaderAPIHooks::g_original_ConMsg = nullptr;
ShaderAPIHooks::DrawIndexedPrimitive_t ShaderAPIHooks::g_original_DrawIndexedPrimitive = nullptr;
ShaderAPIHooks::SetVertexShaderConstantF_t ShaderAPIHooks::g_original_SetVertexShaderConstantF = nullptr;
ShaderAPIHooks::SetStreamSource_t ShaderAPIHooks::g_original_SetStreamSource = nullptr;
ShaderAPIHooks::SetVertexShader_t ShaderAPIHooks::g_original_SetVertexShader = nullptr;
ShaderAPIHooks::DivisionFunction_t ShaderAPIHooks::g_original_DivisionFunction = nullptr;
ShaderAPIHooks::VertexBufferLock_t ShaderAPIHooks::g_original_VertexBufferLock = nullptr;
std::unordered_set<uintptr_t> ShaderAPIHooks::s_problematicAddresses;
ShaderAPIHooks::ParticleRender_t ShaderAPIHooks::g_original_ParticleRender = nullptr;
std::map<uint64_t, uint32_t> ShaderAPIHooks::s_sequenceStarts;
ShaderAPIHooks::LoadMaterial_t ShaderAPIHooks::g_original_LoadMaterial = nullptr;
bool ShaderAPIHooks::s_inOcclusionProxy = false;

// Initialize function pointers
ShaderAPIHooks::FindMaterial_t ShaderAPIHooks::g_original_FindMaterial = nullptr;
ShaderAPIHooks::BeginRenderPass_t ShaderAPIHooks::g_original_BeginRenderPass = nullptr;

ShaderAPIHooks::CreateMaterial_t ShaderAPIHooks::g_original_CreateMaterial = nullptr;
ShaderAPIHooks::GetHardwareConfig_t ShaderAPIHooks::g_original_GetHardwareConfig = nullptr;
ShaderAPIHooks::InitMaterialSystem_t ShaderAPIHooks::g_original_InitMaterialSystem = nullptr;
ShaderAPIHooks::InitProxyMaterial_t ShaderAPIHooks::g_original_InitProxyMaterial = nullptr;

namespace {
    bool IsValidPointer(const void* ptr, size_t size) {
        if (!ptr) return false;
        MEMORY_BASIC_INFORMATION mbi = { 0 };
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return false;
        return true;
    }
}

LONG WINAPI GlobalExceptionHandler(EXCEPTION_POINTERS* ExceptionInfo) {
    if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO) {
        Warning("[Shader Fixes] Global handler caught division by zero at %p\n",
            ExceptionInfo->ExceptionRecord->ExceptionAddress);

        // Skip the faulting instruction
        ExceptionInfo->ContextRecord->Rip += 2;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void ShaderAPIHooks::Initialize() {
    try {
        // Install global exception handler first
        SetUnhandledExceptionFilter(GlobalExceptionHandler);

        
        HMODULE shaderapidx9 = GetModuleHandle("shaderapidx9.dll");
        if (!shaderapidx9) {
            Error("[Shader Fixes] Failed to get shaderapidx9.dll module\n");
            return;
        }

        // Add more specific patterns from the crash
        static const std::pair<const char*, const char*> signatures[] = {
            {"48 63 C8 99 F7 F9", "Division instruction"},
            {"89 51 34 89 38 48 89 D9", "Function entry"},
            {"8B F2 44 0F B6 C0", "Parameter setup"},
            {"F7 F9 03 C1 0F AF C1", "Division and multiply"},
            // The exact sequence from your crash
            {"42 89 44 24 20 44 89 44 24 28", "Pre-crash sequence"},
            {"48 8D 4C 24 20 E8", "Call sequence"}
        };

        for (const auto& sig : signatures) {
            void* found_ptr = ScanSign(shaderapidx9, sig.first, strlen(sig.first));
            if (found_ptr) {
                Msg("[Shader Fixes] Found %s at %p\n", sig.second, found_ptr);

                // Log surrounding bytes
                unsigned char* bytes = reinterpret_cast<unsigned char*>(found_ptr);
                Msg("[Shader Fixes] Bytes at %s: ", sig.second);
                for (int i = -8; i <= 8; i++) {
                    Msg("%02X ", bytes[i]);
                }
                Msg("\n");

                // If this is a division instruction, hook it
                if (strstr(sig.second, "Division")) {
                    Detouring::Hook::Target target(found_ptr);
                    m_DivisionFunction_hook.Create(target, DivisionFunction_detour);
                    g_original_DivisionFunction = m_DivisionFunction_hook.GetTrampoline<DivisionFunction_t>();
                    m_DivisionFunction_hook.Enable();
                    Msg("[Shader Fixes] Hooked division at %p\n", found_ptr);
                }
            }
        }

        // Add SEH handler for the entire module
        AddVectoredExceptionHandler(1, [](PEXCEPTION_POINTERS exceptionInfo) -> LONG {
            if (exceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO) {
                void* crashAddress = exceptionInfo->ExceptionRecord->ExceptionAddress;
                Warning("[Shader Fixes] Caught division by zero at %p\n", crashAddress);
                
                // Get register values
                Warning("Register values at crash:\n");
                Warning("RAX: %016llX\n", exceptionInfo->ContextRecord->Rax);
                Warning("RCX: %016llX\n", exceptionInfo->ContextRecord->Rcx);
                Warning("RDX: %016llX\n", exceptionInfo->ContextRecord->Rdx);
                
                // Modify registers to prevent crash
                exceptionInfo->ContextRecord->Rax = 1;  // Set result to 1
                exceptionInfo->ContextRecord->Rip += 2; // Skip the division instruction
                
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            return EXCEPTION_CONTINUE_SEARCH;
        });

        // Add material system hooks
        if (materials) {
            void** vtable = *reinterpret_cast<void***>(materials);
            if (vtable) {
                // Hook LoadMaterial (typically index 71)
                void* loadMaterialFunc = vtable[71];
                if (loadMaterialFunc) {
                    Detouring::Hook::Target target(loadMaterialFunc);
                    m_LoadMaterial_hook.Create(target, LoadMaterial_detour);
                    g_original_LoadMaterial = m_LoadMaterial_hook.GetTrampoline<LoadMaterial_t>();
                    m_LoadMaterial_hook.Enable();
                    Msg("[Shader Fixes] Hooked LoadMaterial at %p\n", loadMaterialFunc);
                }
            }
        }

        Msg("[Shader Fixes] Enhanced shader protection initialized with exception handler\n");

        // Add vectored exception handler
        PVOID handler = AddVectoredExceptionHandler(1, [](PEXCEPTION_POINTERS exceptionInfo) -> LONG {
            if (exceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO) {
                void* crashAddress = exceptionInfo->ExceptionRecord->ExceptionAddress;
                Warning("[Shader Fixes] Caught division by zero at %p\n", crashAddress);
                
                // Log register state
                Warning("[Shader Fixes] Register state:\n");
                Warning("  RAX: %016llX\n", exceptionInfo->ContextRecord->Rax);
                Warning("  RCX: %016llX\n", exceptionInfo->ContextRecord->Rcx);
                Warning("  RDX: %016llX\n", exceptionInfo->ContextRecord->Rdx);
                Warning("  R8:  %016llX\n", exceptionInfo->ContextRecord->R8);
                Warning("  R9:  %016llX\n", exceptionInfo->ContextRecord->R9);
                Warning("  RIP: %016llX\n", exceptionInfo->ContextRecord->Rip);

                // Try to prevent the crash
                exceptionInfo->ContextRecord->Rax = 1;  // Set result to 1
                exceptionInfo->ContextRecord->Rip += 2; // Skip the division instruction
                
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            return EXCEPTION_CONTINUE_SEARCH;
        });

        if (handler) {
            Msg("[Shader Fixes] Installed vectored exception handler at %p\n", handler);
        }

        // Add material system hooks
        if (materials) {
            void** vtable = *reinterpret_cast<void***>(materials);
            if (vtable) {
                // Hook FindMaterial (typically index 83)
                void* findMaterialFunc = vtable[83];
                if (findMaterialFunc) {
                    Detouring::Hook::Target target(findMaterialFunc);
                    m_FindMaterial_hook.Create(target, FindMaterial_detour);
                    g_original_FindMaterial = m_FindMaterial_hook.GetTrampoline<FindMaterial_t>();
                    m_FindMaterial_hook.Enable();
                    Msg("[Shader Fixes] Hooked FindMaterial at %p\n", findMaterialFunc);
                }

                // Get render context to hook BeginRenderPass
                IMatRenderContext* renderContext = materials->GetRenderContext();
                if (renderContext) {
                    void** renderVtable = *reinterpret_cast<void***>(renderContext);
                    if (renderVtable) {
                        // BeginRenderPass is typically index 105
                        void* beginRenderPassFunc = renderVtable[105];
                        if (beginRenderPassFunc) {
                            Detouring::Hook::Target target(beginRenderPassFunc);
                            m_BeginRenderPass_hook.Create(target, BeginRenderPass_detour);
                            g_original_BeginRenderPass = m_BeginRenderPass_hook.GetTrampoline<BeginRenderPass_t>();
                            m_BeginRenderPass_hook.Enable();
                            Msg("[Shader Fixes] Hooked BeginRenderPass at %p\n", beginRenderPassFunc);
                        }
                    }
                }
            }
        }

        // Find the material system initialization function
        void* matSysInit = FindPattern("materialsystem.dll", "55 8B EC 83 E4 F8 83 EC 18 56 57");
        if (matSysInit) {
            Detouring::Hook::Target target(matSysInit);
            m_InitMaterialSystem_hook.Create(target, InitMaterialSystem_detour);
            g_original_InitMaterialSystem = m_InitMaterialSystem_hook.GetTrampoline<InitMaterialSystem_t>();
            m_InitMaterialSystem_hook.Enable();
        }

        // Find the proxy material initialization function
        void* proxyInit = FindPattern("materialsystem.dll", "55 8B EC 56 8B 75 08 57 8B F9 56 8B 07");
        if (proxyInit) {
            Detouring::Hook::Target target(proxyInit);
            m_InitProxyMaterial_hook.Create(target, InitProxyMaterial_detour);
            g_original_InitProxyMaterial = m_InitProxyMaterial_hook.GetTrampoline<InitProxyMaterial_t>();
            m_InitProxyMaterial_hook.Enable();
        }
        
        // Add hooks for material factory functions
        if (materials) {
            void** vtable = *reinterpret_cast<void***>(materials);
            if (vtable) {
                // Hook CreateMaterial (typically index 72)
                void* createMaterialFunc = vtable[72];
                if (createMaterialFunc) {
                    Detouring::Hook::Target target(createMaterialFunc);
                    m_CreateMaterial_hook.Create(target, CreateMaterial_detour);
                    g_original_CreateMaterial = m_CreateMaterial_hook.GetTrampoline<CreateMaterial_t>();
                    m_CreateMaterial_hook.Enable();
                }
                
                // Hook GetMaterialSystemHardwareConfig (typically index 13)
                void* getHardwareConfigFunc = vtable[13];
                if (getHardwareConfigFunc) {
                    Detouring::Hook::Target target(getHardwareConfigFunc);
                    m_GetHardwareConfig_hook.Create(target, GetHardwareConfig_detour);
                    g_original_GetHardwareConfig = m_GetHardwareConfig_hook.GetTrampoline<GetHardwareConfig_t>();
                    m_GetHardwareConfig_hook.Enable();
                }
            }
        }

        if (InitializeLogging()) {
            LogToFile("Shader protection initialized - hooks installed:\n");
            LogToFile("  DrawIndexedPrimitive: %s\n", m_DrawIndexedPrimitive_hook.IsEnabled() ? "Enabled" : "Disabled");
            LogToFile("  SetVertexShader: %s\n", m_SetVertexShader_hook.IsEnabled() ? "Enabled" : "Disabled");
            LogToFile("  SetStreamSource: %s\n", m_SetStreamSource_hook.IsEnabled() ? "Enabled" : "Disabled");
            LogToFile("  ConMsg: %s\n", s_ConMsg_hook.IsEnabled() ? "Enabled" : "Disabled");
        }        

        // Find D3D9 device
        static const char device_sig[] = "BA E1 0D 74 5E 48 89 1D ?? ?? ?? ??";
        auto device_ptr = ScanSign(shaderapidx9, device_sig, sizeof(device_sig) - 1);
        if (device_ptr) {
            auto offset = ((uint32_t*)device_ptr)[2];
            g_pD3DDevice = *(IDirect3DDevice9**)((char*)device_ptr + offset + 12);
            if (!g_pD3DDevice) {
                Error("[Shader Fixes] Failed to get D3D9 device\n");
            }
        }

        if (!g_pD3DDevice) {
            Error("[Shader Fixes] Failed to find D3D9 device\n");
            return;
        }

        // Get vtable
        void** vftable = *reinterpret_cast<void***>(g_pD3DDevice);
        if (!vftable) {
            Error("[Shader Fixes] Failed to get D3D9 vtable\n");
            return;
        }

        // Hook DrawIndexedPrimitive (index 82)
        Detouring::Hook::Target target_draw(&vftable[82]);
        m_DrawIndexedPrimitive_hook.Create(target_draw, DrawIndexedPrimitive_detour);
        g_original_DrawIndexedPrimitive = m_DrawIndexedPrimitive_hook.GetTrampoline<DrawIndexedPrimitive_t>();
        m_DrawIndexedPrimitive_hook.Enable();

        // Hook SetStreamSource (index 100)
        Detouring::Hook::Target target_stream(&vftable[100]);
        m_SetStreamSource_hook.Create(target_stream, SetStreamSource_detour);
        g_original_SetStreamSource = m_SetStreamSource_hook.GetTrampoline<SetStreamSource_t>();
        m_SetStreamSource_hook.Enable();

        // Hook SetVertexShader (index 92)
        Detouring::Hook::Target target_shader(&vftable[92]);
        m_SetVertexShader_hook.Create(target_shader, SetVertexShader_detour);
        g_original_SetVertexShader = m_SetVertexShader_hook.GetTrampoline<SetVertexShader_t>();
        m_SetVertexShader_hook.Enable();

        // Hook SetVertexShaderConstantF (index 94)
        Detouring::Hook::Target target_const(&vftable[94]);
        m_SetVertexShaderConstantF_hook.Create(target_const, SetVertexShaderConstantF_detour);
        g_original_SetVertexShaderConstantF = m_SetVertexShaderConstantF_hook.GetTrampoline<SetVertexShaderConstantF_t>();
        m_SetVertexShaderConstantF_hook.Enable();

        // Hook ConMsg to catch console messages
        void* conMsg = GetProcAddress(GetModuleHandle("tier0.dll"), "ConMsg");
        if (conMsg) {
            Detouring::Hook::Target target(conMsg);
            s_ConMsg_hook.Create(target, ConMsg_detour);
            g_original_ConMsg = s_ConMsg_hook.GetTrampoline<ConMsg_t>();
            s_ConMsg_hook.Enable();
        }

        Msg("[Shader Fixes] Enhanced shader protection initialized\n");
    }
    catch (...) {
        Error("[Shader Fixes] Failed to initialize shader hooks\n");
    }
}

void* ShaderAPIHooks::FindPattern(const char* module, const char* pattern) {
    HMODULE moduleHandle = GetModuleHandleA(module);
    if (!moduleHandle) {
        Warning("[Shader Fixes] Failed to get module handle for %s\n", module);
        return nullptr;
    }

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), moduleHandle, &moduleInfo, sizeof(moduleInfo))) {
        Warning("[Shader Fixes] Failed to get module information for %s\n", module);
        return nullptr;
    }

    // Convert pattern to bytes
    std::vector<int> bytes;
    const char* start = pattern;
    const char* end = pattern + strlen(pattern);
    
    for (const char* current = start; current < end; ) {
        // Skip whitespace
        while (current < end && isspace(*current)) current++;
        if (current >= end) break;

        // Handle wildcard
        if (*current == '?') {
            bytes.push_back(-1);
            current++;
            continue;
        }

        // Convert hex string to byte
        if (current + 1 < end && isxdigit(*current) && isxdigit(*(current + 1))) {
            char hex[3] = { current[0], current[1], 0 };
            bytes.push_back(strtol(hex, nullptr, 16));
            current += 2;
        }
        else {
            current++;
        }
    }

    if (bytes.empty()) {
        Warning("[Shader Fixes] Invalid pattern: %s\n", pattern);
        return nullptr;
    }

    // Search for pattern
    uint8_t* scanStart = reinterpret_cast<uint8_t*>(moduleHandle);
    uint8_t* scanEnd = scanStart + moduleInfo.SizeOfImage - bytes.size();

    for (uint8_t* current = scanStart; current < scanEnd; current++) {
        bool found = true;
        for (size_t i = 0; i < bytes.size(); i++) {
            if (bytes[i] == -1) continue; // Skip wildcards
            if (current[i] != bytes[i]) {
                found = false;
                break;
            }
        }
        if (found) {
            return current;
        }
    }

    Warning("[Shader Fixes] Pattern not found in module %s: %s\n", module, pattern);
    return nullptr;
}

IMaterial* __fastcall ShaderAPIHooks::CreateMaterial_detour(void* thisptr, void* edx, 
    const char* pMaterialName, KeyValues* pVMTKeyValues) {
    
    if (pMaterialName && strstr(pMaterialName, "occlusion") != nullptr) {
        LogToFile("\n=== Occlusion Proxy Creation Attempt ===\n");
        LogToFile("CreateMaterial called for: %s\n", pMaterialName);
        LogToFile("Return Address: %p\n", _ReturnAddress());
        
        // Log KeyValues if available
        if (pVMTKeyValues) {
            LogToFile("KeyValues contents:\n");
            for (KeyValues* kv = pVMTKeyValues->GetFirstSubKey(); kv; kv = kv->GetNextKey()) {
                LogToFile("  %s = %s\n", kv->GetName(), kv->GetString());
            }
        }
        
        void* callStack[32];
        DWORD framesWritten = CaptureStackBackTrace(0, 32, callStack, nullptr);
        LogToFile("Call Stack:\n");
        LogStackTrace(callStack, framesWritten);
        
        LogToFile("=== End Creation Attempt ===\n\n");
        return g_original_CreateMaterial(thisptr, "debug/debugempty", nullptr);
    }
    
    return g_original_CreateMaterial(thisptr, pMaterialName, pVMTKeyValues);
}

void* __fastcall ShaderAPIHooks::GetHardwareConfig_detour(void* thisptr, void* edx) {
    return g_original_GetHardwareConfig(thisptr);
}

void __fastcall ShaderAPIHooks::ParticleRender_detour(void* thisptr) {
    static float s_lastLogTime = 0.0f;
    float currentTime = GetTickCount64() / 1000.0f;

    __try {
        s_state.isProcessingParticle = true;
        
        // Log every second at most
        if (currentTime - s_lastLogTime > 1.0f) {
            Msg("[Shader Fixes] Particle render called from %p\n", _ReturnAddress());
            s_lastLogTime = currentTime;
        }

        // Add pre-render checks
        if (thisptr) {
            __try {
                // Verify the vtable pointer
                if (!IsValidPointer(thisptr, sizeof(void*))) {
                    Warning("[Shader Fixes] Invalid particle system pointer\n");
                    return;
                }

                void** vtable = *reinterpret_cast<void***>(thisptr);
                if (vtable && IsValidPointer(vtable, sizeof(void*) * 3)) {
                    // Try to get particle system info safely
                    if (vtable[2] && IsValidPointer(vtable[2], sizeof(void*))) {
                        const char* name = reinterpret_cast<const char*>(vtable[2]);
                        Msg("[Shader Fixes] Processing particle system at %p, vtable: %p\n", 
                            thisptr, vtable);
                    }
                }
            }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                Warning("[Shader Fixes] Exception while accessing particle info at %p\n", thisptr);
            }
        }

        // Call original with exception handling
        if (g_original_ParticleRender) {
            __try {
                g_original_ParticleRender(thisptr);
            }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                Warning("[Shader Fixes] Exception in original particle render at %p\n", 
                    _ReturnAddress());
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Warning("[Shader Fixes] Top-level exception in particle rendering\n");
    }

    s_state.isProcessingParticle = false;
}

void ShaderAPIHooks::LogMessage(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Msg("[Shader Fixes] %s", buffer);
}

bool ShaderAPIHooks::InitializeLogging() {
    std::lock_guard<std::mutex> lock(s_logMutex);
    
    try {
        if (s_loggingInitialized) return true;

        // Get the Garry's Mod directory
        char gmodPath[MAX_PATH];
        if (GetModuleFileName(nullptr, gmodPath, MAX_PATH) == 0) {
            Warning("[RTX Fixes] Failed to get module path: %lu\n", GetLastError());
            return false;
        }

        std::string path = gmodPath;
        path = path.substr(0, path.find_last_of("\\/"));
        
        // Create directory paths
        std::string garrysmodPath = path + "\\garrysmod";
        std::string logsPath = garrysmodPath + "\\logs";
        std::string rtxLogsPath = logsPath + "\\rtx_fixes";

        // Create directories
        if (!CreateDirectory(garrysmodPath.c_str(), nullptr) && 
            GetLastError() != ERROR_ALREADY_EXISTS) {
            Warning("[RTX Fixes] Failed to create garrysmod directory\n");
            return false;
        }

        if (!CreateDirectory(logsPath.c_str(), nullptr) && 
            GetLastError() != ERROR_ALREADY_EXISTS) {
            Warning("[RTX Fixes] Failed to create logs directory\n");
            return false;
        }

        if (!CreateDirectory(rtxLogsPath.c_str(), nullptr) && 
            GetLastError() != ERROR_ALREADY_EXISTS) {
            Warning("[RTX Fixes] Failed to create rtx_fixes directory\n");
            return false;
        }

        // Create timestamp for filename
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
        
        // Open log file
        s_logPath = rtxLogsPath + "\\shader_fixes_" + timestamp + ".log";
        s_logFile.open(s_logPath, std::ios::out | std::ios::app);
        
        if (!s_logFile.is_open()) {
            Warning("[RTX Fixes] Failed to open log file: %s\n", s_logPath.c_str());
            return false;
        }

        // Write initial log entry
        s_logFile << "=== RTX Shader Fixes Log Started at " << timestamp << " ===" << std::endl;
        s_logFile << "Path: " << s_logPath << std::endl;
        s_logFile << "Process ID: " << GetCurrentProcessId() << std::endl;
        s_logFile << "=================================================" << std::endl;
        
        s_loggingInitialized = true;
        Warning("[RTX Fixes] Log file initialized at: %s\n", s_logPath.c_str());
        return true;
    }
    catch (const std::exception& e) {
        Warning("[RTX Fixes] Exception in InitializeLogging: %s\n", e.what());
        return false;
    }
    catch (...) {
        Warning("[RTX Fixes] Unknown exception in InitializeLogging\n");
        return false;
    }
}

void ShaderAPIHooks::LogToFile(const char* format, ...) {
    std::lock_guard<std::mutex> lock(s_logMutex);
    
    if (!s_loggingInitialized || !s_logFile.is_open()) {
        return;
    }
    
    try {
        // Get timestamp for log entry
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &timeinfo);
        
        // Format the message
        char buffer[4096];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        // Write to file with timestamp
        s_logFile << "[" << timestamp << "] " << buffer;
        s_logFile.flush();
    }
    catch (...) {
        Warning("[RTX Fixes] Exception in LogToFile\n");
    }
}

int __fastcall ShaderAPIHooks::DivisionFunction_detour(int a1, int a2, int dividend, int divisor) {
    void* const returnAddr = _ReturnAddress();
    const uint64_t currentAddr = reinterpret_cast<uint64_t>(returnAddr);
    const uint64_t lastThreeBytes = currentAddr & 0xFFF;

    // Known crash addresses (last 3 bytes)
    static const uint16_t KNOWN_OFFSETS[] = {
        0x449, // First crash point
        0x4AC, // Second crash point
        0x534, // Third crash point
        0xF3C  // Fourth crash point
    };

    // Check if we're at a known problematic address
    bool isKnownAddress = false;
    for (const auto offset : KNOWN_OFFSETS) {
        if ((lastThreeBytes & 0xFFF) == offset) {
            isKnownAddress = true;
            break;
        }
    }

    // Check if this is part of the occlusion proxy pattern
    bool isOcclusionValue = (
        (dividend >= 0x2FC && dividend <= 0x2FF) ||  // Old range
        (dividend >= 0x47C && dividend <= 0x47F)     // New range
    );

    if (isKnownAddress || isOcclusionValue) {
        if (divisor == 0) {
            // Store the start of each sequence we see
            if ((dividend & 0x3) == 0x3) { // If it's the highest value in the sequence
                s_sequenceStarts[currentAddr] = dividend & ~0x3;
            }

            // Get the base value for this sequence
            uint32_t sequenceBase = s_sequenceStarts[currentAddr];
            uint32_t valueInSequence = dividend & 0x3;

            LogMessage("Handling occlusion sequence:\n"
                      "  Address: %p (offset: %03X)\n"
                      "  Dividend: 0x%X (sequence base: 0x%X, value: %d)\n"
                      "  R8: 0x%X\n"
                      "  R9: 0x%X\n",
                      returnAddr,
                      lastThreeBytes,
                      dividend,
                      sequenceBase,
                      valueInSequence,
                      a1,
                      a2);

            switch (lastThreeBytes & 0xFFF) {
                case 0x449: // First point - maintain sequence
                    return dividend;
                case 0x4AC: // Second point - maintain sequence
                    return dividend;
                case 0x534: // Third point - visibility test
                    return valueInSequence + 1; // Return position in sequence
                case 0xF3C: // Fourth point - final calculation
                    return valueInSequence + 1; // Return position in sequence
                default:
                    return 1;
            }
        }
    }

    // For non-zero divisors, protect against very small values
    if (abs(divisor) < 1) {
        Warning("[Shader Fixes] Very small divisor detected: %d\n", divisor);
        return dividend;
    }

    // Normal division handling
    try {
        int result = dividend / divisor;
        
        // Check for unreasonable results
        if (abs(result) > 10000) {
            Warning("[Shader Fixes] Extremely large division result at %p: %d\n", 
                returnAddr, result);
            return dividend < 0 ? -1 : 1;
        }

        return result;
    }
    catch (...) {
        Warning("[Shader Fixes] Exception in division handler at %p\n", returnAddr);
        return 1;
    }
}


HRESULT __stdcall ShaderAPIHooks::VertexBufferLock_detour(
    void* thisptr,
    UINT offsetToLock,
    UINT sizeToLock,
    void** ppbData,
    DWORD flags) {
    
    __try {
        // Log the attempt
        Msg("[Shader Fixes] CVertexBuffer::Lock - Offset: %u, Size: %u\n", offsetToLock, sizeToLock);

        // Validate parameters before calling original
        if (!thisptr) {
            Warning("[Shader Fixes] CVertexBuffer::Lock failed - null vertex buffer\n");
            return E_FAIL;
        }

        // Check for division-prone calculations
        if (sizeToLock > 0 && offsetToLock > 0) {
            UINT divCheck = offsetToLock / sizeToLock;
            if (divCheck == 0) {
                Warning("[Shader Fixes] CVertexBuffer::Lock - Potential division by zero prevented\n");
                return E_FAIL;
            }
        }

        return g_original_VertexBufferLock(thisptr, offsetToLock, sizeToLock, ppbData, flags);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Warning("[Shader Fixes] Exception in CVertexBuffer::Lock at %p\n", _ReturnAddress());
        return E_FAIL;
    }
}

void ShaderAPIHooks::Shutdown() {
    m_DrawIndexedPrimitive_hook.Disable();
    m_SetVertexShaderConstantF_hook.Disable();
    m_SetStreamSource_hook.Disable();
    m_SetVertexShader_hook.Disable();
    s_ConMsg_hook.Disable();
    m_FindMaterial_hook.Disable();
    m_BeginRenderPass_hook.Disable();
    m_LoadMaterial_hook.Disable();    
    
    if (s_logFile.is_open()) {
        s_logFile << "\n=== RTX Shader Fixes Log Ended ===\n";
        s_logFile.close();
    }
}

void __cdecl ShaderAPIHooks::ConMsg_detour(const char* fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // Check for shader/particle error messages
    if (strstr(buffer, "C_OP_RenderSprites") ||
        strstr(buffer, "shader") ||
        strstr(buffer, "particle") ||
        strstr(buffer, "material")) {
        
        s_state.lastErrorMessage = buffer;
        s_state.lastErrorTime = GetTickCount64() / 1000.0f;
        s_state.isProcessingParticle = true;

        // Extract material name if present
        std::regex materialRegex("Material ([^\\s]+)");
        std::smatch matches;
        std::string bufferStr(buffer);
        if (std::regex_search(bufferStr, matches, materialRegex)) {
            std::string materialName = matches[1].str();
            s_problematicMaterials.insert(materialName);
            Warning("[Shader Fixes] Added problematic material: %s\n", materialName.c_str());
        }
    }

    if (g_original_ConMsg) {
        g_original_ConMsg("%s", buffer);
    }
}

HRESULT __stdcall ShaderAPIHooks::DrawIndexedPrimitive_detour(
    IDirect3DDevice9* device,
    D3DPRIMITIVETYPE PrimitiveType,
    INT BaseVertexIndex,
    UINT MinVertexIndex,
    UINT NumVertices,
    UINT StartIndex,
    UINT PrimitiveCount) {
    
    __try {
        // Skip occlusion proxy completely
        if (materials && materials->GetRenderContext()) {
            IMaterial* currentMaterial = materials->GetRenderContext()->GetCurrentMaterial();
            if (currentMaterial && currentMaterial->GetName()) {
                const char* matName = currentMaterial->GetName();
                if (strcmp(matName, "engine/occlusionproxy") == 0 ||
                    strstr(matName, "occlusionproxy") != nullptr) {
                    LogMessage("Skipping occlusion proxy draw call\n");
                    return D3D_OK;
                }
            }
        }

        if (s_state.isProcessingParticle || IsParticleSystem()) {
            if (!ValidatePrimitiveParams(MinVertexIndex, NumVertices, PrimitiveCount)) {
                Warning("[Shader Fixes] Blocked invalid draw call for %s\n", 
                    s_state.lastMaterialName.c_str());
                return D3D_OK;
            }
        }

        return g_original_DrawIndexedPrimitive(
            device, PrimitiveType, BaseVertexIndex, MinVertexIndex,
            NumVertices, StartIndex, PrimitiveCount);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Warning("[Shader Fixes] Exception in DrawIndexedPrimitive for %s\n", 
            s_state.lastMaterialName.c_str());
        return D3D_OK;
    }
}

bool __fastcall ShaderAPIHooks::InitMaterialSystem_detour(void* thisptr, void* edx, void* hardwareConfig, void* adapter, const char* materialBasedir) {
    LogToFile("Material system initialization intercepted\n");
    
    // Log the call stack with module information
    void* callStack[32];
    DWORD framesWritten = CaptureStackBackTrace(0, 32, callStack, nullptr);
    
    LogToFile("Material system initialization call stack:\n");
    LogStackTrace(callStack, framesWritten);
    LogToFile("\n");
    
    // Fix: Add proper argument types and cast string to void*
    return InitMaterialSystem_trampoline()(
        thisptr,
        edx,
        hardwareConfig,
        adapter,
        materialBasedir
    );
}

void ShaderAPIHooks::LogStackTrace(void* const* callStack, DWORD frameCount) {
    HMODULE modules[1024];
    DWORD cbNeeded;
    
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &cbNeeded)) {
        DWORD numModules = cbNeeded / sizeof(HMODULE);
        
        for (DWORD i = 0; i < frameCount; i++) {
            DWORD64 addr = (DWORD64)callStack[i];
            bool foundModule = false;
            
            for (DWORD j = 0; j < numModules; j++) {
                MODULEINFO modInfo;
                if (GetModuleInformation(GetCurrentProcess(), modules[j], &modInfo, sizeof(modInfo))) {
                    if (addr >= (DWORD64)modInfo.lpBaseOfDll && 
                        addr < (DWORD64)modInfo.lpBaseOfDll + modInfo.SizeOfImage) {
                        char modName[MAX_PATH];
                        GetModuleFileNameEx(GetCurrentProcess(), modules[j], modName, MAX_PATH);
                        
                        std::string modulePath = modName;
                        size_t pos = modulePath.find_last_of("\\/");
                        std::string moduleBaseName = (pos == std::string::npos) ? 
                            modulePath : modulePath.substr(pos + 1);
                        
                        LogToFile("  [%d] %p in %s (+0x%llX)\n", 
                            i, 
                            (void*)addr, 
                            moduleBaseName.c_str(),
                            addr - (DWORD64)modInfo.lpBaseOfDll);
                        foundModule = true;
                        break;
                    }
                }
            }
            
            if (!foundModule) {
                LogToFile("  [%d] %p (unknown module)\n", i, (void*)addr);
            }
        }
    }
}

void __fastcall ShaderAPIHooks::InitProxyMaterial_detour(void* proxyData) {
    LogMessage("Proxy material initialization intercepted\n");
    
    // Log the call stack
    void* callStack[32];
    DWORD framesWritten = CaptureStackBackTrace(0, 32, callStack, nullptr);
    
    LogMessage("Proxy material initialization call stack:\n");
    for (DWORD i = 0; i < framesWritten; i++) {
        DWORD64 addr = (DWORD64)callStack[i];
        LogMessage("  [%d] %p\n", i, (void*)addr);
    }
    
    // Skip the original initialization
    // InitProxyMaterial_trampoline()(proxyData);
}

IMaterial* __fastcall ShaderAPIHooks::FindMaterial_detour(void* thisptr, void* edx, 
    const char* materialName, const char* textureGroupName, bool complain, const char* complainPrefix) {
    
    if (materialName && strstr(materialName, "occlusion") != nullptr) {
        LogToFile("\n=== Occlusion Proxy Material Request ===\n");
        LogToFile("FindMaterial called for: %s\n", materialName);
        LogToFile("Return Address: %p\n", _ReturnAddress());
        
        // Log stack trace
        void* callStack[32];
        DWORD framesWritten = CaptureStackBackTrace(0, 32, callStack, nullptr);
        LogToFile("Call Stack:\n");
        LogStackTrace(callStack, framesWritten);
        
        LogToFile("=== End Occlusion Proxy Request ===\n\n");
        return g_original_FindMaterial(thisptr, edx, "debug/debugempty", "Other", false, nullptr);
    }
    
    return g_original_FindMaterial(thisptr, edx, materialName, textureGroupName, complain, complainPrefix);
}

void __fastcall ShaderAPIHooks::BeginRenderPass_detour(IMatRenderContext* thisptr, void* edx, IMaterial* material) {
    if (!material) {
        return;
    }

    const char* matName = material->GetName();
    if (matName && (
        strcmp(matName, "engine/occlusionproxy") == 0 ||
        strstr(matName, "occlusionproxy") != nullptr ||
        s_inOcclusionProxy)) {
        
        LogToFile("\n=== Occlusion Proxy Render Attempt ===\n");
        LogToFile("BeginRenderPass called for: %s\n", matName);
        LogToFile("Shader Name: %s\n", material->GetShaderName());
        LogToFile("Return Address: %p\n", _ReturnAddress());
        
        void* callStack[32];
        DWORD framesWritten = CaptureStackBackTrace(0, 32, callStack, nullptr);
        LogToFile("Call Stack:\n");
        LogStackTrace(callStack, framesWritten);
        
        LogToFile("=== End Render Attempt ===\n\n");
        return;
    }

    g_original_BeginRenderPass(thisptr, edx, material);
}

// Add implementation of LoadMaterial_detour:
IMaterial* __fastcall ShaderAPIHooks::LoadMaterial_detour(void* thisptr, void* edx, 
    const char* materialName, const char* textureGroupName) {
    
    if (materialName && (
        strcmp(materialName, "engine/occlusionproxy") == 0 ||
        strstr(materialName, "occlusionproxy") != nullptr)) {
        
        LogToFile("\n=== Occlusion Proxy Load Attempt ===\n");
        LogToFile("LoadMaterial called for: %s\n", materialName);
        LogToFile("Texture Group: %s\n", textureGroupName ? textureGroupName : "none");
        LogToFile("Return Address: %p\n", _ReturnAddress());
        
        void* callStack[32];
        DWORD framesWritten = CaptureStackBackTrace(0, 32, callStack, nullptr);
        LogToFile("Call Stack:\n");
        LogStackTrace(callStack, framesWritten);
        
        LogToFile("=== End Load Attempt ===\n\n");
        
        s_inOcclusionProxy = true;
        IMaterial* replacement = g_original_LoadMaterial(thisptr, "debug/debugempty", "Other");
        s_inOcclusionProxy = false;
        return replacement;
    }

    return g_original_LoadMaterial(thisptr, materialName, textureGroupName);
}


HRESULT __stdcall ShaderAPIHooks::SetVertexShaderConstantF_detour(
    IDirect3DDevice9* device,
    UINT StartRegister,
    CONST float* pConstantData,
    UINT Vector4fCount) {
    
    __try {
        if (s_state.isProcessingParticle || IsParticleSystem()) {
            if (!ValidateShaderConstants(pConstantData, Vector4fCount, nullptr)) { // Add nullptr here
                Warning("[Shader Fixes] Blocked invalid shader constants for %s\n",
                    s_state.lastMaterialName.c_str());
                return D3D_OK;
            }
        }

        return g_original_SetVertexShaderConstantF(
            device, StartRegister, pConstantData, Vector4fCount);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Warning("[Shader Fixes] Exception in SetVertexShaderConstantF\n");
        return D3D_OK;
    }
}

HRESULT __stdcall ShaderAPIHooks::SetStreamSource_detour(
    IDirect3DDevice9* device,
    UINT StreamNumber,
    IDirect3DVertexBuffer9* pStreamData,
    UINT OffsetInBytes,
    UINT Stride) {
    
    __try {
        // Skip occlusion proxy
        if (materials && materials->GetRenderContext()) {
            IMaterial* currentMaterial = materials->GetRenderContext()->GetCurrentMaterial();
            if (IsOcclusionProxy(currentMaterial)) {
                return D3D_OK;
            }
        }

        if (s_state.isProcessingParticle || IsParticleSystem()) {
            if (pStreamData && !ValidateParticleVertexBuffer(pStreamData, Stride)) {
                Warning("[Shader Fixes] Blocked invalid vertex buffer for %s\n",
                    s_state.lastMaterialName.c_str());
                return D3D_OK;
            }
        }

        return g_original_SetStreamSource(device, StreamNumber, pStreamData, OffsetInBytes, Stride);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Warning("[Shader Fixes] Exception in SetStreamSource\n");
        return D3D_OK;
    }
}

HRESULT __stdcall ShaderAPIHooks::SetVertexShader_detour(
    IDirect3DDevice9* device,
    IDirect3DVertexShader9* pShader) {
    
    __try {
        if (materials && materials->GetRenderContext()) {
            IMaterial* currentMaterial = materials->GetRenderContext()->GetCurrentMaterial();
            if (IsOcclusionProxy(currentMaterial)) {
                return D3D_OK;
            }
        }

        if (s_state.isProcessingParticle || IsParticleSystem()) {
            if (!ValidateVertexShader(pShader)) {
                Warning("[Shader Fixes] Blocked invalid vertex shader for %s\n",
                    s_state.lastMaterialName.c_str());
                return D3D_OK;
            }
        }

        return g_original_SetVertexShader(device, pShader);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Warning("[Shader Fixes] Exception in SetVertexShader\n");
        return D3D_OK;
    }
}

bool ShaderAPIHooks::ValidateVertexBuffer(
    IDirect3DVertexBuffer9* pVertexBuffer,
    UINT offsetInBytes,
    UINT stride) {
    
    // Log validation attempt
    Msg("[Shader Fixes] Validating vertex buffer:\n"
        "  Offset: %u\n"
        "  Stride: %u\n"
        "  Buffer: %p\n",
        offsetInBytes, stride, pVertexBuffer);

    if (!pVertexBuffer) return false;

    D3DVERTEXBUFFER_DESC bufferDesc;  // Changed variable name here
    if (FAILED(pVertexBuffer->GetDesc(&bufferDesc))) return false;

    Msg("[Shader Fixes] Buffer description:\n"
        "  Size: %u\n"
        "  FVF: %u\n"
        "  Type: %d\n",
        bufferDesc.Size, bufferDesc.FVF, bufferDesc.Type);

    // Check for potentially dangerous calculations
    if (stride == 0) {
        Warning("[Shader Fixes] Zero stride detected in vertex buffer\n");
        return false;
    }

    if (offsetInBytes >= bufferDesc.Size) {
        Warning("[Shader Fixes] Offset (%u) exceeds buffer size (%u)\n", 
            offsetInBytes, bufferDesc.Size);
        return false;
    }

    // Check division safety
    if (bufferDesc.Size > 0 && stride > 0) {
        UINT vertexCount = bufferDesc.Size / stride;
        if (vertexCount == 0) {
            Warning("[Shader Fixes] Invalid vertex count calculation prevented\n");
            return false;
        }
    }

    void* data;
    if (SUCCEEDED(pVertexBuffer->Lock(offsetInBytes, stride, &data, D3DLOCK_READONLY))) {
        bool valid = true;
        float* floatData = static_cast<float*>(data);
        
        try {
            for (UINT i = 0; i < stride/sizeof(float); i++) {
                // Log suspicious values
                if (!_finite(floatData[i]) || _isnan(floatData[i])) {
                    Warning("[Shader Fixes] Invalid float at offset %u: %f (addr: %p)\n", 
                        i * sizeof(float), floatData[i], &floatData[i]);
                    valid = false;
                    break;
                }
            }
        }
        catch (...) {
            Warning("[Shader Fixes] Exception during vertex buffer validation\n");
            valid = false;
        }
        
        pVertexBuffer->Unlock();
        return valid;
    }

    return false;
}

bool ShaderAPIHooks::ValidateParticleVertexBuffer(IDirect3DVertexBuffer9* pVertexBuffer, UINT stride) {
    if (!pVertexBuffer) return false;

    D3DVERTEXBUFFER_DESC desc;
    if (FAILED(pVertexBuffer->GetDesc(&desc))) return false;

    void* data;
    if (SUCCEEDED(pVertexBuffer->Lock(0, desc.Size, &data, D3DLOCK_READONLY))) {
        bool valid = true;
        float* floatData = static_cast<float*>(data);
        
        // Enhanced validation for particle data
        for (UINT i = 0; i < desc.Size / sizeof(float); i++) {
            // Check for invalid values
            if (!_finite(floatData[i]) || _isnan(floatData[i])) {
                Warning("[Shader Fixes] Invalid float detected at index %d: %f\n", i, floatData[i]);
                valid = false;
                break;
            }
            // Check for unreasonable values
            if (fabsf(floatData[i]) > 1e6) {
                Warning("[Shader Fixes] Unreasonable value detected at index %d: %f\n", i, floatData[i]);
                valid = false;
                break;
            }
            // Check for potential divide by zero
            if (fabsf(floatData[i]) < 1e-6) {
                Warning("[Shader Fixes] Near-zero value detected at index %d: %f\n", i, floatData[i]);
                valid = false;
                break;
            }
        }
        
        pVertexBuffer->Unlock();
        return valid;
    }

    return false;
}

bool ShaderAPIHooks::ValidateShaderConstants(const float* pConstantData, UINT Vector4fCount, const char* shaderName) {
    if (!pConstantData || Vector4fCount == 0) return false;

    // Get current material
    IMaterial* currentMaterial = nullptr;
    if (materials && materials->GetRenderContext()) {
        currentMaterial = materials->GetRenderContext()->GetCurrentMaterial();
    }

    // Special handling for occlusion proxy
    bool isOcclusionProxy = currentMaterial && 
        strcmp(currentMaterial->GetName(), "engine/occlusionproxy") == 0;

    if (isOcclusionProxy) {
        LogMessage("Validating occlusion proxy constants:\n"
                  "  Vector4f Count: %d\n"
                  "  Return Address: %p\n",
                  Vector4fCount,
                  _ReturnAddress());
    }

    for (UINT i = 0; i < Vector4fCount * 4; i++) {
        // Check for invalid values
        if (!_finite(pConstantData[i]) || _isnan(pConstantData[i])) {
            Warning("[Shader Fixes] Invalid shader constant at index %d: %f\n", i, pConstantData[i]);
            return false;
        }

        // For occlusion proxy, ensure we don't have zero values that might cause division
        if (isOcclusionProxy && fabsf(pConstantData[i]) < 1e-6) {
            LogMessage("  Fixing zero constant in occlusion proxy at index %d\n", i);
            const_cast<float*>(pConstantData)[i] = 1.0f;
        }
    }

    return true;
}

bool ShaderAPIHooks::ValidatePrimitiveParams(
    UINT MinVertexIndex,
    UINT NumVertices,
    UINT PrimitiveCount) {
    
    if (NumVertices == 0 || PrimitiveCount == 0) {
        Warning("[Shader Fixes] Zero vertices or primitives\n");
        return false;
    }
    if (MinVertexIndex >= NumVertices) {
        Warning("[Shader Fixes] MinVertexIndex (%d) >= NumVertices (%d)\n", 
            MinVertexIndex, NumVertices);
        return false;
    }

    // Additional checks for particle system primitives
    if (PrimitiveCount > 10000) {
        Warning("[Shader Fixes] Excessive primitive count: %d\n", PrimitiveCount);
        return false;
    }

    return true;
}

bool ShaderAPIHooks::ValidateVertexShader(IDirect3DVertexShader9* pShader) {
    if (!pShader) return false;

    // Basic shader validation
    UINT functionSize = 0;
    if (FAILED(pShader->GetFunction(nullptr, &functionSize)) || functionSize == 0) {
        Warning("[Shader Fixes] Invalid shader function size\n");
        return false;
    }

    // Additional validation could be added here
    return true;
}

bool ShaderAPIHooks::IsOcclusionProxy(IMaterial* material) {
    if (!material || !material->GetName()) {
        return false;
    }
    return strcmp(material->GetName(), "engine/occlusionproxy") == 0;
}

void ShaderAPIHooks::HandleOcclusionProxy() {
    static float lastHandleTime = 0;
    float currentTime = GetTickCount64() / 1000.0f;

    // Only handle once per second to avoid spam
    if (currentTime - lastHandleTime < 1.0f) {
        return;
    }
    lastHandleTime = currentTime;

    if (!materials || !materials->GetRenderContext()) {
        return;
    }

    IMaterial* currentMaterial = materials->GetRenderContext()->GetCurrentMaterial();
    if (!currentMaterial || !currentMaterial->GetName() || 
        strcmp(currentMaterial->GetName(), "engine/occlusionproxy") != 0) {
        return;
    }

    // Try to force a valid shader
    static const char* const SHADER_ATTEMPTS[] = {
        "UnlitGeneric",
        "VertexLitGeneric",
        "Wireframe",
        "Debug"
    };
    static const size_t SHADER_COUNT = sizeof(SHADER_ATTEMPTS) / sizeof(SHADER_ATTEMPTS[0]);

    bool shaderSet = false;
    for (size_t i = 0; i < SHADER_COUNT; i++) {
        KeyValues* pKeyValues = new KeyValues(SHADER_ATTEMPTS[i]);
        if (!pKeyValues) {
            continue;
        }

        pKeyValues->SetString("$basetexture", "dev/flat");
        pKeyValues->SetInt("$translucent", 0);
        pKeyValues->SetInt("$nocull", 1);
        pKeyValues->SetInt("$ignorez", 0);
        
        // Call SetShaderAndParams without checking its return value
        currentMaterial->SetShaderAndParams(pKeyValues);
        
        // Check if the shader was successfully set by verifying the shader name
        const char* currentShader = currentMaterial->GetShaderName();
        shaderSet = (currentShader && strcmp(currentShader, SHADER_ATTEMPTS[i]) == 0);
        
        pKeyValues->deleteThis();
        
        if (shaderSet) {
            LogMessage("Successfully set shader '%s' for occlusion proxy\n", SHADER_ATTEMPTS[i]);
            break;
        }
    }

    if (!shaderSet) {
        Warning("[Shader Fixes] Failed to set any shader for occlusion proxy\n");
    }

    // Force immediate update
    currentMaterial->Refresh();
    
    // Log after refresh
    LogMessage("Applied occlusion proxy fixes:\n"
              "  Material: %s\n"
              "  Shader: %s\n"
              "  Return Address: %p\n",
              currentMaterial->GetName(),
              currentMaterial->GetShaderName(),
              _ReturnAddress());
}


bool ShaderAPIHooks::IsParticleSystem() {
    try {
        if (!materials) {
            return false;
        }

        IMatRenderContext* renderContext = materials->GetRenderContext();
        if (!renderContext) {
            return false;
        }

        IMaterial* currentMaterial = renderContext->GetCurrentMaterial();
        if (!currentMaterial) {
            return false;
        }

        const char* materialName = currentMaterial->GetName();
        const char* shaderName = currentMaterial->GetShaderName();
        
        // Make sure we check for null before strcmp
        if (materialName && strcmp(materialName, "engine/occlusionproxy") == 0) {
            HandleOcclusionProxy();
            if (shaderName) {
                LogMessage("Occlusion proxy material in use:\n"
                          "  Return Address: %p\n"
                          "  Shader: %s\n",
                          _ReturnAddress(),
                          shaderName);
            }
            return true;
        }

        UpdateShaderState(materialName, shaderName);

        // Check if we're within the error window
        float currentTime = GetTickCount64() / 1000.0f;
        if (currentTime - s_state.lastErrorTime < 0.1f) {
            return true;
        }

        // Check against known problematic materials
        if (materialName && s_problematicMaterials.find(materialName) != s_problematicMaterials.end()) {
            return true;
        }

        // Check shader name against known problematic patterns
        if (shaderName && IsKnownProblematicShader(shaderName)) {
            return true;
        }

        // Check blend states using global D3D device
        if (g_pD3DDevice) {
            DWORD srcBlend, destBlend, zEnable;
            g_pD3DDevice->GetRenderState(D3DRS_SRCBLEND, &srcBlend);
            g_pD3DDevice->GetRenderState(D3DRS_DESTBLEND, &destBlend);
            g_pD3DDevice->GetRenderState(D3DRS_ZENABLE, &zEnable);

            if ((srcBlend == D3DBLEND_SRCALPHA && destBlend == D3DBLEND_INVSRCALPHA) ||
                (srcBlend == D3DBLEND_ONE && destBlend == D3DBLEND_ONE) ||
                zEnable == D3DZB_FALSE) {
                return true;
            }
        }
    }
    catch (...) {
        Warning("[Shader Fixes] Exception in IsParticleSystem\n");
    }
    
    return false;
}

void ShaderAPIHooks::UpdateShaderState(const char* materialName, const char* shaderName) {
    if (materialName) {
        s_state.lastMaterialName = materialName;
    }
    if (shaderName) {
        s_state.lastShaderName = shaderName;
    }
}

bool ShaderAPIHooks::IsKnownProblematicShader(const char* name) {
    if (!name) return false;

    for (const auto& pattern : s_knownProblematicShaders) {
        if (strstr(name, pattern.c_str())) {
            return true;
        }
    }
    return false;
}

void ShaderAPIHooks::AddProblematicShader(const char* name) {
    if (name) {
        s_knownProblematicShaders.insert(name);
        Warning("[Shader Fixes] Added problematic shader: %s\n", name);
    }
}

void ShaderAPIHooks::LogShaderError(const char* format, ...) {
    static float lastLogTime = 0.0f;
    float currentTime = GetTickCount64() / 1000.0f;

    if (currentTime - lastLogTime < 1.0f) return;
    lastLogTime = currentTime;

    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Warning("[Shader Fixes] %s", buffer);
}