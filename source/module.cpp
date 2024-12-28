#include "GarrysMod/Lua/Interface.h"
#include <remix/remix.h>
#include <remix/remix_c.h>
#include "cdll_client_int.h"
#include "materialsystem/imaterialsystem.h"
#include <shaderapi/ishaderapi.h>
#include "e_utils.h"
#include <Windows.h>
#include <d3d9.h>
#include "tier0/dbg.h"
#include "rtx_lights/rtx_light_manager.h"
#include "shader_fixes/shader_hooks.h"
#include "fvf/fixed_function_renderer.h"

#ifdef GMOD_MAIN
extern IMaterialSystem* materials = NULL;
#endif

// extern IShaderAPI* g_pShaderAPI = NULL;
remix::Interface* g_remix = nullptr;

static ConVar rtx_ff_enable("rtx_ff_enable", "0", FCVAR_ARCHIVE, "Enable fixed function pipeline");
static ConVar rtx_ff_debug("rtx_ff_debug", "0", FCVAR_ARCHIVE, "Enable extra debug output for fixed function pipeline");

// ConVar callback
void FF_EnableChanged(IConVar *var, const char *pOldValue, float flOldValue) {
    Msg("[Fixed Function] State changed to: %d\n", static_cast<ConVar*>(var)->GetBool());
}

using namespace GarrysMod::Lua;

// Lua function to toggle fixed function
LUA_FUNCTION(FF_Enable) {
    bool enable = LUA->GetBool(1);
    rtx_ff_enable.SetValue(enable);
    Msg("[Fixed Function] %s via Lua\n", enable ? "Enabled" : "Disabled");
    return 0;
}

LUA_FUNCTION(FF_GetStats) {
    // Create table for stats
    LUA->CreateTable();
    
    // Add some basic stats
    LUA->PushNumber(0); // Example stat
    LUA->SetField(-2, "total_draws");
    
    LUA->PushNumber(0); // Example stat
    LUA->SetField(-2, "ff_draws");
    
    return 1;
}

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
        Msg("[RTX FVF] Starting module initialization...\n");

        // Find D3D device
        auto sourceDevice = static_cast<IDirect3DDevice9Ex*>(FindD3D9Device());
        if (!sourceDevice) {
            Error("[RTX FVF] Failed to find D3D9 device\n");
            return 1; // Return error
        }
        Msg("[RTX FVF] Found D3D9 device: %p\n", sourceDevice);

        // Initialize fixed function renderer
        try {
            FixedFunctionRenderer::Instance().Initialize(sourceDevice);
            Msg("[RTX FVF] Fixed function renderer initialized\n");
        }
        catch (const std::exception& e) {
            Error("[RTX FVF] Failed to initialize renderer: %s\n", e.what());
            return 1;
        }

        // Setup Lua interface
        try {
            // Create Lua interface
            Msg("[RTX FVF] Setting up Lua interface...\n");
            
            LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
            
            // Create FF table
            LUA->CreateTable();
            
            // Add Enable function
            LUA->PushCFunction(FF_Enable);
            LUA->SetField(-2, "Enable");
            
            // Add version info
            LUA->PushString("1.0");
            LUA->SetField(-2, "Version");
            
            // Set the table in _G
            LUA->SetField(-2, "FixedFunction");
            
            // Pop global table
            LUA->Pop();
            
            Msg("[RTX FVF] Lua interface setup complete\n");
        }
        catch (const std::exception& e) {
            Error("[RTX FVF] Failed to setup Lua interface: %s\n", e.what());
            return 1;
        }

        // Register ConVars
        try {
            static ConVar rtx_ff_enable("rtx_ff_enable", "1", FCVAR_ARCHIVE, "Enable fixed function pipeline", true, 0, true, 1);
            rtx_ff_enable.InstallChangeCallback(FF_EnableChanged);
            Msg("[RTX FVF] ConVars registered\n");
        }
        catch (const std::exception& e) {
            Error("[RTX FVF] Failed to register ConVars: %s\n", e.what());
            return 1;
        }

        Msg("[RTX FVF] Module initialized successfully\n");
        return 0;
    }
    catch (const std::exception& e) {
        Error("[RTX FVF] Unhandled exception during initialization: %s\n", e.what());
        return 1;
    }
    catch (...) {
        Error("[RTX FVF] Unknown error during initialization\n");
        return 1;
    }
}

GMOD_MODULE_CLOSE() {
    try {
        Msg("[RTX FVF] Shutting down...\n");
        FixedFunctionRenderer::Instance().Shutdown();
        Msg("[RTX FVF] Shutdown complete\n");
        return 0;
    }
    catch (const std::exception& e) {
        Error("[RTX FVF] Error during shutdown: %s\n", e.what());
        return 1;
    }
    catch (...) {
        Error("[RTX FVF] Unknown error during shutdown\n");
        return 1;
    }
}