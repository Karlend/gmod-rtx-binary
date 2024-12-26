#include "GarrysMod/Lua/Interface.h"
#include <remix/remix.h>
#include <remix/remix_c.h>
#include "cdll_client_int.h"
#include "materialsystem/imaterialsystem.h"
#include <shaderapi/ishaderapi.h>
#include "e_utils.h"
#include <Windows.h>
#include <d3d9.h>
#include "rtx_lights/rtx_light_manager.h"
#include "shader_fixes/shader_hooks.h"
#include "fvf_compat/material_converter.h"
#include "utils/interfaces.h"

#ifdef GMOD_MAIN
#endif

// extern IShaderAPI* g_pShaderAPI = NULL;
remix::Interface* g_remix = nullptr;

using namespace GarrysMod::Lua;

LUA_FUNCTION(CreateRTXLight) {
    try {
        if (!g_remix) {
            Msg("[RTX Remix Fixes] Remix interface is null\n");
            LUA->ThrowError("[RTX Remix Fixes] - Remix interface is null");
            return 0;
        }

        float x = LUA->CheckNumber(1);
        float y = LUA->CheckNumber(2);
        float z = LUA->CheckNumber(3);
        float size = LUA->CheckNumber(4);
        float brightness = LUA->CheckNumber(5);
        float r = LUA->CheckNumber(6);
        float g = LUA->CheckNumber(7);
        float b = LUA->CheckNumber(8);

        // Debug print received values
        Msg("[RTX Light Module] Received values - Pos: %.2f,%.2f,%.2f, Size: %f, Brightness: %f, Color: %f,%f,%f\n",
            x, y, z, size, brightness, r, g, b);

        auto props = RTXLightManager::LightProperties();
        props.x = x;
        props.y = y;
        props.z = z;
        props.size = size;
        props.brightness = brightness;
        props.r = r / 255.0f;
        props.g = g / 255.0f;
        props.b = b / 255.0f;

        auto& manager = RTXLightManager::Instance();
        auto handle = manager.CreateLight(props);
        if (!handle) {
            Msg("[RTX Light Module] Failed to create light!\n");
            LUA->ThrowError("[RTX Remix Fixes] - Failed to create light");
            return 0;
        }

        Msg("[RTX Light Module] Light created successfully with handle %p\n", handle);
        LUA->PushUserdata(handle);
        return 1;
    }
    catch (...) {
        Msg("[RTX Light Module] Exception in CreateRTXLight\n");
        LUA->ThrowError("[RTX Remix Fixes] - Exception in light creation");
        return 0;
    }
}


LUA_FUNCTION(UpdateRTXLight) {
    try {
        if (!g_remix) {
            Msg("[RTX Remix Fixes] Remix interface is null\n");
            LUA->ThrowError("[RTX Remix Fixes] - Remix interface is null");
            return 0;
        }

        auto handle = static_cast<remixapi_LightHandle>(LUA->GetUserdata(1));
        if (!handle) {
            Msg("[RTX Remix Fixes] Invalid light handle\n");
            LUA->ThrowError("[RTX Remix Fixes] - Invalid light handle");
            return 0;
        }

        float x = LUA->CheckNumber(2);
        float y = LUA->CheckNumber(3);
        float z = LUA->CheckNumber(4);
        float size = LUA->CheckNumber(5);
        float brightness = LUA->CheckNumber(6);
        float r = LUA->CheckNumber(7);
        float g = LUA->CheckNumber(8);
        float b = LUA->CheckNumber(9);

        Msg("[RTX Remix Fixes] Updating light at (%f, %f, %f) with size %f and brightness %f\n", 
            x, y, z, size, brightness);

        auto props = RTXLightManager::LightProperties();
        props.x = x;
        props.y = y;
        props.z = z;
        props.size = size < 1.0f ? 1.0f : size;
        props.brightness = brightness < 0.1f ? 0.1f : brightness;
        props.r = (r / 255.0f) > 1.0f ? 1.0f : (r / 255.0f < 0.0f ? 0.0f : r / 255.0f);
        props.g = (g / 255.0f) > 1.0f ? 1.0f : (g / 255.0f < 0.0f ? 0.0f : g / 255.0f);
        props.b = (b / 255.0f) > 1.0f ? 1.0f : (b / 255.0f < 0.0f ? 0.0f : b / 255.0f);

        auto& manager = RTXLightManager::Instance();
        if (!manager.UpdateLight(handle, props)) {
            Msg("[RTX Remix Fixes] Failed to update light\n");
            LUA->ThrowError("[RTX Remix Fixes] - Failed to update light");
            return 0;
        }

        LUA->PushUserdata(handle);
        return 1;
    }
    catch (...) {
        Msg("[RTX Remix Fixes] Exception in UpdateRTXLight\n");
        LUA->ThrowError("[RTX Remix Fixes] - Exception in light update");
        return 0;
    }
}

LUA_FUNCTION(DestroyRTXLight) {
    try {
        auto handle = static_cast<remixapi_LightHandle>(LUA->GetUserdata(1));
        RTXLightManager::Instance().DestroyLight(handle);
        return 0;
    }
    catch (...) {
        Msg("[RTX Remix Fixes] Exception in DestroyRTXLight\n");
        return 0;
    }
}

LUA_FUNCTION(DrawRTXLights) { 
    try {
        if (!g_remix) {
            Msg("[RTX Remix Fixes] Cannot draw lights - Remix interface is null\n");
            return 0;
        }

        RTXLightManager::Instance().DrawLights();
        return 0;
    }
    catch (...) {
        Msg("[RTX Remix Fixes] Exception in DrawRTXLights\n");
        return 0;
    }
}

void* FindD3D9Device() {
    auto shaderapidx = GetModuleHandle("shaderapidx9.dll");
    if (!shaderapidx) {
        Error("[RTX] Failed to get shaderapidx9.dll module\n");
        return nullptr;
    }

    Msg("[RTX] shaderapidx9.dll module: %p\n", shaderapidx);

    static const char sign[] = "BA E1 0D 74 5E 48 89 1D ?? ?? ?? ??";
    auto ptr = ScanSign(shaderapidx, sign, sizeof(sign) - 1);
    if (!ptr) { 
        Error("[RTX] Failed to find D3D9Device signature\n");
        return nullptr;
    }

    auto offset = ((uint32_t*)ptr)[2];
    auto device = *(IDirect3DDevice9Ex**)((char*)ptr + offset + 12);
    if (!device) {
        Error("[RTX] D3D9Device pointer is null\n");
        return nullptr;
    }

    return device;
}

GMOD_MODULE_OPEN() { 
    try {
        Msg("[RTX Remix Fixes 2] - Module loaded!\n"); 

        // Get the engine factory
        CreateInterfaceFn engineFactory = Sys_GetFactory("engine.dll");
        if (!engineFactory) {
            Error("[Interface Debug] Failed to get engine factory\n");
            return 0;
        }
        Msg("[Interface Debug] Got engine factory at %p\n", engineFactory);

        // Get the material system factory
        CreateInterfaceFn materialSystemFactory = Sys_GetFactory("materialsystem.dll");
        if (!materialSystemFactory) {
            Error("[Interface Debug] Failed to get material system factory\n");
            return 0;
        }
        Msg("[Interface Debug] Got material system factory at %p\n", materialSystemFactory);

        // Try different material system versions
        const char* materialSystemVersions[] = {
            "VMaterialSystem082",
            "VMaterialSystem081",
            "VMaterialSystem080"
        };

        void* materialSystemRaw = nullptr;
        const char* successVersion = nullptr;

        for (const char* version : materialSystemVersions) {
            Msg("[Interface Debug] Trying material system version: %s\n", version);
            materialSystemRaw = materialSystemFactory(version, nullptr);
            if (materialSystemRaw) {
                successVersion = version;
                break;
            }
        }

        if (!materialSystemRaw) {
            Error("[Interface Debug] Failed to get material system interface with any version\n");
            return 0;
        }

        Msg("[Interface Debug] Successfully got material system with version: %s\n", successVersion);

        // Cast to IMaterialSystem
        materials = static_cast<IMaterialSystem*>(materialSystemRaw);
        if (!materials) {
            Error("[Interface Debug] Failed to cast material system interface\n");
            return 0;
        }

        Msg("[Interface Debug] Material system interface initialized at %p\n", materials);

        // Test material system functionality
        IMaterial* testMaterial = materials->FindMaterial("debug/debugempty", TEXTURE_GROUP_OTHER);
        if (!testMaterial) {
            Error("[Interface Debug] Failed to find test material\n");
            return 0;
        }

        Msg("[Interface Debug] Successfully tested material system functionality\n");

        if (!MaterialConverter::VerifyMaterialSystem()) {
            Error("[RTX Fixes] Material system verification failed\n");
            return 0;
        }

        Msg("[Hook Debug] Starting material system hook setup...\n");
        Msg("[Hook Debug] Material system interface: %p\n", materials);

        try {
            void** vtable = *reinterpret_cast<void***>(materials);
            if (!vtable) {
                Error("[Hook Debug] Failed to get vtable - null pointer\n");
                return 0;
            }
            Msg("[Hook Debug] Found vtable at %p\n", vtable);

            // Log vtable entries
            for (int i = 80; i < 86; i++) {
                Msg("[Hook Debug] vtable[%d] = %p\n", i, vtable[i]);
            }

            void* findMaterialFunc = vtable[83];
            if (!findMaterialFunc) {
                Error("[Hook Debug] FindMaterial function pointer is null\n");
                return 0;
            }
            Msg("[Hook Debug] Found FindMaterial at %p\n", findMaterialFunc);

            static Detouring::Hook findMaterialHook;
            Detouring::Hook::Target target(findMaterialFunc);

            Msg("[Hook Debug] Creating detour...\n");
            if (!findMaterialHook.Create(target, MaterialSystem_FindMaterial_detour)) {
                Error("[Hook Debug] Failed to create hook\n");
                return 0;
            }
            
            g_original_FindMaterial = findMaterialHook.GetTrampoline<FindMaterial_t>();
            if (!g_original_FindMaterial) {
                Error("[Hook Debug] Failed to get trampoline function\n");
                return 0;
            }

            findMaterialHook.Enable();
            Msg("[Hook Debug] Hook enabled successfully\n");

            // Test the hook
            IMaterial* testMat = materials->FindMaterial("debug/debugempty", TEXTURE_GROUP_OTHER, true);
            if (!testMat) {
                Error("[Hook Debug] Hook test failed - couldn't find test material\n");
                return 0;
            }
            
            Msg("[Hook Debug] Hook test successful\n");

            if (!MaterialConverter::Instance().Initialize()) {
                Error("[Hook Debug] Failed to initialize material converter\n");
                return 0;
            }

            Msg("[RTX Fixes] Module initialization completed successfully\n");
            return 1;  // Return 1 for success
        }
        catch (const std::exception& e) {
            Error("[Hook Debug] Exception during hook setup: %s\n", e.what());
            return 0;
        }
        catch (...) {
            Error("[Hook Debug] Unknown exception during hook setup\n");
            return 0;
        }
    }
    catch (...) {
        Error("[RTX] Exception in module initialization\n");
        return 0;
    }
}

GMOD_MODULE_CLOSE() {
    try {
        Msg("[RTX] Shutting down module...\n");

                // Shutdown shader protection
        //ShaderAPIHooks::Instance().Shutdown();

        Msg("[RTX] Module shutdown complete\n");
        return 0;
    }
    catch (...) {
        Error("[RTX] Exception in module shutdown\n");
        return 0;
    }
}