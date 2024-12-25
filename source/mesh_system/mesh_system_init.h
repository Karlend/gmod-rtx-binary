#pragma once

namespace MeshSystem {
    // Initialize all mesh system components
    bool Initialize(GarrysMod::Lua::ILuaBase* LUA);
    
    // Shutdown all mesh system components
    void Shutdown();
}