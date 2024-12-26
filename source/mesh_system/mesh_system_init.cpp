#include "mesh_system_init.h"
#include "mesh_manager.h"
#include <tier0/dbg.h>
#include <algorithm>

// Global interfaces used by mesh system
IMaterialSystem* g_materials = nullptr;
IVEngineClient* g_engine = nullptr;
IVModelInfo* g_modelinfo = nullptr;
IClientEntityList* g_entitylist = nullptr;

extern IVDebugOverlay* debugoverlay;
extern ICvar* cvar;

namespace MeshSystem {

bool Initialize(GarrysMod::Lua::ILuaBase* LUA) {
    Msg("[Mesh System] Initializing...\n");
    
    // Get required interfaces
    CreateInterfaceFn engineFactory = Sys_GetFactory("engine.dll");
    CreateInterfaceFn clientFactory = Sys_GetFactory("client.dll");
    CreateInterfaceFn materialSystemFactory = Sys_GetFactory("materialsystem.dll");
    
    if (!engineFactory || !clientFactory || !materialSystemFactory) {
        Error("[Mesh System] Failed to get required factory interfaces\n");
        return false;
    }
    
    g_materials = (IMaterialSystem*)materialSystemFactory(MATERIAL_SYSTEM_INTERFACE_VERSION, NULL);
    g_engine = (IVEngineClient*)engineFactory(VENGINE_CLIENT_INTERFACE_VERSION, NULL);
    g_modelinfo = (IVModelInfo*)engineFactory(VMODELINFO_CLIENT_INTERFACE_VERSION, NULL);
    g_entitylist = (IClientEntityList*)clientFactory(VCLIENTENTITYLIST_INTERFACE_VERSION, NULL);

    if (!g_materials || !g_engine || !g_modelinfo || !g_entitylist) {
        Error("[Mesh System] Failed to get required interfaces\n");
        return false;
    }

    // Register Lua functions
    MeshManager::Instance().RegisterLuaFunctions(LUA);
    
    Msg("[Mesh System] Initialized successfully\n");
    return true;
}

void Shutdown() {
    Msg("[Mesh System] Shutting down...\n");
    
    // Shutdown mesh manager
    MeshManager::Instance().Shutdown();
    
    // Clear interface pointers
    g_materials = nullptr;
    g_engine = nullptr;
    g_modelinfo = nullptr;
    g_entitylist = nullptr;
    
    Msg("[Mesh System] Shutdown complete\n");
}

} // namespace MeshSystem