#pragma once
#include "GarrysMod/Lua/Interface.h"
#include "../mesh_system/mesh_manager.h"

namespace MeshSystemLua {
    void Initialize(GarrysMod::Lua::ILuaBase* LUA);
    
    // Only declare the functions here, don't define them
    extern LUA_FUNCTION(EnableCustomRendering);
    extern LUA_FUNCTION(DisableCustomRendering);
    extern LUA_FUNCTION(RebuildMeshes);
    extern LUA_FUNCTION(GetRenderStats);
    extern LUA_FUNCTION(SetChunkSize);
    extern LUA_FUNCTION(GetChunkSize);
    extern LUA_FUNCTION(SetDebugMode);
    extern LUA_FUNCTION(GetDebugMode);
    extern LUA_FUNCTION(GetTotalVertexCount);
    extern LUA_FUNCTION(GetChunkCount);
    extern LUA_FUNCTION(GetDrawCalls);
    extern LUA_FUNCTION(IsMeshSystemEnabled);
    extern LUA_FUNCTION(GetMaterialCount);
    extern LUA_FUNCTION(SetMaxVerticesPerChunk);
}