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
    // Always return false to disable culling
    return false;
}

Define_Hook(bool, ShouldDraw, IClientRenderable* pRenderable, const Vector& vecAbsMin, 
    const Vector& vecAbsMax, const Vector* pVecCenter, Frustum_t frustum) {
    // Always return true to force drawing
    return true;
}

Define_Hook(void, BuildWorldLists, IVRenderView* renderView, ExtendedWorldListInfo_t* pInfo, 
    WorldListLeafData_t* pLeafData, int viewId) {
    if (pInfo) {
        // Cast to our extended type to access the additional field
        ExtendedWorldListInfo_t* extInfo = static_cast<ExtendedWorldListInfo_t*>(pInfo);
        extInfo->m_nRenderFlags |= (RTX_RENDER_FLAGS_FORCE_NO_VIS | 
                                  RTX_RENDER_FLAGS_DISABLE_RENDERING_CACHE);
    }
    BuildWorldLists_trampoline()(renderView, pInfo, pLeafData, viewId);
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

        // Find and hook culling functions
        void* cullBoxAddr = nullptr;
        void* shouldDrawAddr = nullptr;
        void* buildWorldListsAddr = nullptr;

        HMODULE clientModule = GetModuleHandle("client.dll");
        if (clientModule) {
            // Signatures for the functions we want to hook
            static const char cullBoxSig[] = "55 8B EC 8B 45 08 F3 0F 10 00";
            static const char shouldDrawSig[] = "55 8B EC 83 EC 18 56 8B F1 57";
            static const char buildWorldListsSig[] = "55 8B EC 83 EC 30 53 56 57";

            cullBoxAddr = ScanSign(clientModule, cullBoxSig, sizeof(cullBoxSig) - 1);
            shouldDrawAddr = ScanSign(clientModule, shouldDrawSig, sizeof(shouldDrawSig) - 1);
            buildWorldListsAddr = ScanSign(clientModule, buildWorldListsSig, sizeof(buildWorldListsSig) - 1);
        }

        // Set up hooks
        if (cullBoxAddr) {
            Setup_Hook(CullBox, cullBoxAddr);
            Msg("[RTX Remix Fixes 2] Hooked CullBox at %p\n", cullBoxAddr);
        }

        if (shouldDrawAddr) {
            Setup_Hook(ShouldDraw, shouldDrawAddr);
            Msg("[RTX Remix Fixes 2] Hooked ShouldDraw at %p\n", shouldDrawAddr);
        }

        if (buildWorldListsAddr) {
            Setup_Hook(BuildWorldLists, buildWorldListsAddr);
            Msg("[RTX Remix Fixes 2] Hooked BuildWorldLists at %p\n", buildWorldListsAddr);
        }

        // Set up studio render hook (existing code)
        auto studiorenderdll = GetModuleHandle("studiorender.dll");
        if (studiorenderdll) {
            static const char sign[] = "48 89 54 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50";
            auto R_StudioSetupSkinAndLighting = ScanSign(studiorenderdll, sign, sizeof(sign) - 1);
            if (R_StudioSetupSkinAndLighting) {
                Setup_Hook(R_StudioSetupSkinAndLighting, R_StudioSetupSkinAndLighting);
            }
        }

        // Disable engine culling flags
        ModifyRenderFlags(true);

        Msg("[RTX Remix Fixes 2] Render hooks initialized successfully\n");
    }
    catch (...) {
        Error("[RTX Remix Fixes 2] Exception in ModelRenderHooks::Initialize\n");
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