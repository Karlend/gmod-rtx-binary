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

#ifdef GMOD_MAIN
extern IMaterialSystem* materials = NULL;
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
            LUA->PushNil();
            return 1;
        }

        // Convert handle to integer value
        uint64_t handleValue = (uint64_t)handle;
        LUA->PushNumber(static_cast<double>(handleValue));
        
        Msg("[RTX Light Module] Light created successfully with handle %p\n", handle);
        return 1;
    }
    catch (...) {
        Msg("[RTX Light Module] Exception in CreateRTXLight\n");
        LUA->PushNil();
        return 1;
    }
}



LUA_FUNCTION(UpdateRTXLight) {
    try {
        if (!g_remix) {
            LUA->ThrowError("[RTX Remix Fixes] - Remix interface is null");
            return 0;
        }

        // Convert number back to handle
        uint64_t handleValue = static_cast<uint64_t>(LUA->CheckNumber(1));
        auto handle = (remixapi_LightHandle)handleValue;
        
        if (!handle) {
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
        if (!manager.UpdateLight(handle, props)) {
            LUA->ThrowError("[RTX Remix Fixes] - Failed to update light");
            return 0;
        }

        // Return the same handle value
        LUA->PushNumber(static_cast<double>(handleValue));
        return 1;
    }
    catch (...) {
        Msg("[RTX Remix Fixes] Exception in UpdateRTXLight\n");
        return 0;
    }
}

LUA_FUNCTION(QueueRTXLightUpdate) {
    try {
        // Get the handle - convert from number to handle pointer
        uint64_t handleValue = static_cast<uint64_t>(LUA->CheckNumber(1));
        remixapi_LightHandle handle = reinterpret_cast<remixapi_LightHandle>(handleValue);

        // Read position
        float x = static_cast<float>(LUA->CheckNumber(2));
        float y = static_cast<float>(LUA->CheckNumber(3));
        float z = static_cast<float>(LUA->CheckNumber(4));
        float size = static_cast<float>(LUA->CheckNumber(5));
        float brightness = static_cast<float>(LUA->CheckNumber(6));
        float r = static_cast<float>(LUA->CheckNumber(7));
        float g = static_cast<float>(LUA->CheckNumber(8));
        float b = static_cast<float>(LUA->CheckNumber(9));
        bool force = LUA->GetBool(10); // Optional parameter, defaults to false in Lua

        RTXLightManager::LightProperties props;
        props.x = x;
        props.y = y;
        props.z = z;
        props.size = size;
        props.brightness = brightness;
        props.r = r;
        props.g = g;
        props.b = b;

        RTXLightManager::Instance().QueueLightUpdate(handle, props, force);
        
        return 0;
    }
    catch (...) {
        Msg("[RTX Light Module] Exception in QueueRTXLightUpdate\n");
        return 0;
    }
}

LUA_FUNCTION(DestroyRTXLight) {
    try {
        // Convert number back to handle
        uint64_t handleValue = static_cast<uint64_t>(LUA->CheckNumber(1));
        auto handle = (remixapi_LightHandle)handleValue;
        
        if (handle) {
            RTXLightManager::Instance().DestroyLight(handle);
        }
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

        // Create metatable for light handles
        LUA->CreateMetaTable("RTXLightHandle");
        
        LUA->PushCFunction([](lua_State* L) -> int {
            GarrysMod::Lua::ILuaBase* LUA = L->luabase;
            void** userData = (void**)LUA->GetUserdata(1);
            if (userData && *userData) {
                RTXLightManager::Instance().DestroyLight((remixapi_LightHandle)*userData);
            }
            return 0;
        });
        LUA->SetField(-2, "__gc");
        
        LUA->Pop();

        // Initialize shader protection
        ShaderAPIHooks::Instance().Initialize();

        // Find Source's D3D9 device
        auto sourceDevice = static_cast<IDirect3DDevice9Ex*>(FindD3D9Device());
        if (!sourceDevice) {
            LUA->ThrowError("[RTX] Failed to find D3D9 device");
            return 0;
        }

        // Initialize Remix
        if (auto interf = remix::lib::loadRemixDllAndInitialize(L"d3d9.dll")) {
            g_remix = new remix::Interface{ *interf };
        }
        else {
            LUA->ThrowError("[RTX Remix Fixes 2] - remix::loadRemixDllAndInitialize() failed"); 
        }

        g_remix->dxvk_RegisterD3D9Device(sourceDevice);

        // Initialize RTX Light Manager
        RTXLightManager::Instance().Initialize(g_remix);

        // Configure RTX settings
        if (g_remix) {
            g_remix->SetConfigVariable("rtx.enableAdvancedMode", "1");
            g_remix->SetConfigVariable("rtx.fallbackLightMode", "2");
            Msg("[RTX Remix Fixes] RTX configuration set\n");
        }

        // Register Lua functions
        LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB); 
            LUA->PushCFunction(CreateRTXLight);
            LUA->SetField(-2, "CreateRTXLight");
            
            LUA->PushCFunction(UpdateRTXLight);
            LUA->SetField(-2, "UpdateRTXLight");
            
            LUA->PushCFunction(DestroyRTXLight);
            LUA->SetField(-2, "DestroyRTXLight");
            
            LUA->PushCFunction(DrawRTXLights);
            LUA->SetField(-2, "DrawRTXLights");

            LUA->PushCFunction(QueueRTXLightUpdate);
            LUA->SetField(-2, "QueueRTXLightUpdate");
        LUA->Pop();

        return 0;
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
        ShaderAPIHooks::Instance().Shutdown();
        
        RTXLightManager::Instance().Shutdown();

        if (g_remix) {
            delete g_remix;
            g_remix = nullptr;
        }

        Msg("[RTX] Module shutdown complete\n");
        return 0;
    }
    catch (...) {
        Error("[RTX] Exception in module shutdown\n");
        return 0;
    }
}