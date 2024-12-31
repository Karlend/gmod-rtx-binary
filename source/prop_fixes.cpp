#include "GarrysMod/Lua/Interface.h"  
#include "e_utils.h"  
#include "materialsystem/imaterialsystem.h"
#include "interfaces/interfaces.h"  
#include "prop_fixes.h"  
#include "ivrenderview.h"
#include "cdll_int.h"

using namespace GarrysMod::Lua;

// Original function pointers
typedef bool (*CullBox_t)(const Vector&, const Vector&);
typedef bool (*ShouldDraw_t)(IClientRenderable*, const Vector&, const Vector&, const Vector*, Frustum_t);
typedef void (*BuildWorldLists_t)(IVRenderView*, WorldListInfo_t*, WorldListLeafData_t*, int);

void DumpMemory(void* addr, int bytes) {
    unsigned char* data = (unsigned char*)addr;
    char buffer[1024];
    char* ptr = buffer;
    
    ptr += sprintf(ptr, "Memory at %p: ", addr);
    for (int i = 0; i < bytes; i++) {
        ptr += sprintf(ptr, "%02X ", data[i]);
    }
    
    Msg("%s\n", buffer);
}

bool ValidateFunction(void* addr, const char* name) {
    try {
        if (!addr) {
            Warning("[RTX Remix Fixes 2] %s address is null\n", name);
            return false;
        }

        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) {
            Warning("[RTX Remix Fixes 2] %s VirtualQuery failed: %d\n", name, GetLastError());
            return false;
        }

        if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            Warning("[RTX Remix Fixes 2] %s memory is not executable: %d\n", name, mbi.Protect);
            return false;
        }

        // Check if the address points to valid code
        unsigned char* code = (unsigned char*)addr;
        if (code[0] == 0xCC || // int3
            (code[0] == 0x33 && code[1] == 0xC0) || // xor eax, eax
            (code[0] == 0xC3)) // ret
        {
            Warning("[RTX Remix Fixes 2] %s appears to be a stub function\n", name);
            return false;
        }

        // Dump first 16 bytes of the function
        DumpMemory(addr, 16);

        return true;
    }
    catch (...) {
        Warning("[RTX Remix Fixes 2] Exception during %s validation\n", name);
        return false;
    }
}

bool IsValidPointer(const void* ptr) {
    if (!ptr) return false;
    
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return false;
    
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return false;
    
    return true;
}

void ModelRenderHooks::ModifyRenderFlags(bool enable) {
    if (!m_pRenderView) return;

    try {
        // The render flags are typically stored near the beginning of the class
        const int POSSIBLE_FLAG_OFFSETS[] = { 0x4, 0x8, 0xC, 0x10 };
        
        for (int offset : POSSIBLE_FLAG_OFFSETS) {
            DWORD oldProtect;
            if (VirtualProtect((char*)m_pRenderView + offset, sizeof(DWORD), 
                             PAGE_READWRITE, &oldProtect)) {
                DWORD* flags = (DWORD*)((char*)m_pRenderView + offset);
                
                if (enable) {
                    *flags |= (RTX_RENDER_FLAGS_FORCE_NO_VIS | 
                             RTX_RENDER_FLAGS_DISABLE_RENDERING_CACHE);
                } else {
                    *flags &= ~(RTX_RENDER_FLAGS_FORCE_NO_VIS | 
                              RTX_RENDER_FLAGS_DISABLE_RENDERING_CACHE);
                }

                VirtualProtect((char*)m_pRenderView + offset, sizeof(DWORD), 
                             oldProtect, &oldProtect);
            }
        }
    } catch (...) {
        Error("[RTX Prop Fixes] Exception in ModifyRenderFlags\n");
    }
}

void ModelRenderHooks::SetNoVisFlags(bool enable) {
    m_noVisEnabled = enable;
    ModifyRenderFlags(enable);
}

bool ModelRenderHooks::HasNoVisFlags() const {
    return m_noVisEnabled;
}

Define_Hook(bool, CullBox, const Vector& mins, const Vector& maxs) {
    static bool firstCall = true;
    if (firstCall) {
        Msg("[RTX Remix Fixes 2] CullBox first call - this=%p, mins=(%f,%f,%f), maxs=(%f,%f,%f)\n",
            _ReturnAddress(), mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z);
        firstCall = false;
    }
    
    __try {
        return false;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Warning("[RTX Remix Fixes 2] Exception in CullBox, caller=%p\n", _ReturnAddress());
        return false;
    }
}

Define_Hook(bool, ShouldDraw, IClientRenderable* pRenderable, const Vector& vecAbsMin, 
    const Vector& vecAbsMax, const Vector* pVecCenter, Frustum_t frustum) {
    __try {
        return true; // Always force drawing
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("[RTX Remix Fixes 2] Exception in ShouldDraw hook\n");
        return true;
    }
}

Define_Hook(void, BuildWorldLists, IVRenderView* renderView, ExtendedWorldListInfo_t* pInfo, 
    WorldListLeafData_t* pLeafData, int viewId) {
    __try {
        if (pInfo && IsValidPointer(pInfo)) {
            pInfo->m_nRenderFlags |= (RTX_RENDER_FLAGS_FORCE_NO_VIS | 
                                    RTX_RENDER_FLAGS_DISABLE_RENDERING_CACHE);
        }
        BuildWorldLists_trampoline()(renderView, pInfo, pLeafData, viewId);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Msg("[RTX Remix Fixes 2] Exception in BuildWorldLists hook\n");
    }
}

Define_method_Hook(IMaterial*, R_StudioSetupSkinAndLighting, void*, IMatRenderContext* pRenderContext, 
    int index, IMaterial** ppMaterials, int materialFlags,
    void /*IClientRenderable*/* pClientRenderable, void* pColorMeshes, void* lighting)
{
    IMaterial* ret = R_StudioSetupSkinAndLighting_trampoline()(_this, pRenderContext, index, 
        ppMaterials, materialFlags, pClientRenderable, pColorMeshes, lighting);
    lighting = 0; // LIGHTING_HARDWARE 
    materialFlags = 0;
    return ret;
}

void ModelRenderHooks::Initialize() {
    try { 
        Msg("[RTX Remix Fixes 2] - Loading render hooks\n");

        // Get necessary interfaces with proper casting
        auto engineFactory = Sys_GetFactory("engine.dll");
        if (engineFactory) {
            m_pRenderView = static_cast<IVRenderView*>(engineFactory("VEngineRenderView014", nullptr));
            m_pModelRender = static_cast<IVModelRender*>(engineFactory("VEngineModel016", nullptr));
        }

        if (!m_pRenderView || !m_pModelRender) {
            Error("[RTX Remix Fixes 2] Failed to get render interfaces\n");
            return;
        }

        Msg("[RTX Remix Fixes 2] Got render interfaces: RenderView=%p, ModelRender=%p\n",
            m_pRenderView, m_pModelRender);

        HMODULE clientModule = GetModuleHandle("client.dll");
        if (!clientModule) {
            Error("[RTX Remix Fixes 2] Failed to get client.dll module\n");
            return;
        }

        Msg("[RTX Remix Fixes 2] client.dll base: %p\n", clientModule);

        void* cullBoxAddr = nullptr;
        void* shouldDrawAddr = nullptr;
        void* buildWorldListsAddr = nullptr;

        struct FunctionInfo {
            const char* name;
            const char* signature;
            size_t sigLength;
            void** address;
            const char* expectedBytes;
        };

        FunctionInfo functions[] = {
            {
                "CullBox",
                "40 53 48 83 EC 20 48 8B D9 48 8B 89",
                12,
                &cullBoxAddr,
                "40 53 48 83 EC 20 48 8B D9"  // Expected starting bytes
            },
            {
                "ShouldDraw",
                "40 53 48 83 EC 40 48 8B D9 48 8B 89",
                12,
                &shouldDrawAddr,
                "40 53 48 83 EC 40 48 8B D9"
            },
            {
                "BuildWorldLists",
                "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 40",
                20,
                &buildWorldListsAddr,
                "48 89 5C 24"
            }
        };

        for (const auto& func : functions) {
            void* addr = ScanSign(clientModule, func.signature, func.sigLength);
            if (addr) {
                Msg("[RTX Remix Fixes 2] Found %s at %p\n", func.name, addr);
                
                // Validate the function
                if (!ValidateFunction(addr, func.name)) {
                    Warning("[RTX Remix Fixes 2] %s validation failed\n", func.name);
                    continue;
                }

                // Compare with expected bytes
                unsigned char* code = (unsigned char*)addr;
                bool matchesExpected = true;
                for (size_t i = 0; i < strlen(func.expectedBytes)/3; i++) {
                    char expected[3];
                    memcpy(expected, func.expectedBytes + i*3, 2);
                    expected[2] = 0;
                    unsigned char expectedByte = (unsigned char)strtol(expected, nullptr, 16);
                    if (code[i] != expectedByte) {
                        matchesExpected = false;
                        break;
                    }
                }

                if (!matchesExpected) {
                    Warning("[RTX Remix Fixes 2] %s bytes don't match expected pattern\n", func.name);
                    continue;
                }

                *func.address = addr;
                Msg("[RTX Remix Fixes 2] %s validated successfully\n", func.name);
            } else {
                Warning("[RTX Remix Fixes 2] Failed to find %s\n", func.name);
            }
        }

        // Only proceed if all functions were found and validated
        if (!cullBoxAddr || !shouldDrawAddr || !buildWorldListsAddr) {
            Error("[RTX Remix Fixes 2] One or more functions not found\n");
            return;
        }

        // Hook setup with additional validation
        bool success = true;
        
        try {
            // Hook CullBox
            Setup_Hook(CullBox, cullBoxAddr);
            void* hookAddr = *(void**)cullBoxAddr;
            DumpMemory(hookAddr, 16);
            Msg("[RTX Remix Fixes 2] CullBox hook installed, trampoline at %p\n", 
                CullBox_trampoline());
        } catch (...) {
            success = false;
            Warning("[RTX Remix Fixes 2] CullBox hook failed\n");
        }

        if (success) {
            try {
                // Hook ShouldDraw
                Setup_Hook(ShouldDraw, shouldDrawAddr);
                void* hookAddr = *(void**)shouldDrawAddr;
                DumpMemory(hookAddr, 16);
                Msg("[RTX Remix Fixes 2] ShouldDraw hook installed, trampoline at %p\n", 
                    ShouldDraw_trampoline());
            } catch (...) {
                success = false;
                Warning("[RTX Remix Fixes 2] ShouldDraw hook failed\n");
            }
        }

        if (success) {
            try {
                // Hook BuildWorldLists
                Setup_Hook(BuildWorldLists, buildWorldListsAddr);
                void* hookAddr = *(void**)buildWorldListsAddr;
                DumpMemory(hookAddr, 16);
                Msg("[RTX Remix Fixes 2] BuildWorldLists hook installed, trampoline at %p\n", 
                    BuildWorldLists_trampoline());
            } catch (...) {
                success = false;
                Warning("[RTX Remix Fixes 2] BuildWorldLists hook failed\n");
            }
        }

        // Set up studio render hook
        auto studiorenderdll = GetModuleHandle("studiorender.dll");
        if (studiorenderdll) {
            static const char sign[] = "48 89 54 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50";
            auto R_StudioSetupSkinAndLighting = ScanSign(studiorenderdll, sign, sizeof(sign) - 1);
            if (R_StudioSetupSkinAndLighting) {
                if (ValidateFunction(R_StudioSetupSkinAndLighting, "R_StudioSetupSkinAndLighting")) {
                    Setup_Hook(R_StudioSetupSkinAndLighting, R_StudioSetupSkinAndLighting);
                    Msg("[RTX Remix Fixes 2] Hooked R_StudioSetupSkinAndLighting at %p\n", R_StudioSetupSkinAndLighting);
                }
            }
        }

        // Only modify render flags if all hooks were successful
        if (success) {
            try {
                ModifyRenderFlags(true);
                m_noVisEnabled = true;
                Msg("[RTX Remix Fixes 2] Successfully modified render flags\n");
            } catch (...) {
                Warning("[RTX Remix Fixes 2] Failed to modify render flags\n");
                success = false;
            }
        }

        if (!success) {
            Warning("[RTX Remix Fixes 2] Some initialization failed, attempting cleanup\n");
            Shutdown();
            return;
        }

        Msg("[RTX Remix Fixes 2] Render hooks initialization complete\n");
    }
    catch (...) {
        Error("[RTX Remix Fixes 2] Exception in initialization\n");
        Shutdown();
    }
}

void ModelRenderHooks::Shutdown() {
    m_CullBox_hook.Disable();
    m_ShouldDraw_hook.Disable();
    m_BuildWorldLists_hook.Disable();
    m_StudioSkinLighting_hook.Disable();

    ModifyRenderFlags(false);
    Msg("[Prop Fixes] Shutdown complete\n");
}