#include "mesh_system_lua.h"
#include "mesh_manager.h"
#include <tier0/dbg.h>
#include "materialsystem/imaterial.h"
#include <algorithm>

namespace MeshSystemLua {

void Initialize(GarrysMod::Lua::ILuaBase* LUA) {
    Msg("[Mesh System] Initializing Lua bindings...\n");
    
    // Register global functions
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    
        // Core functionality
        LUA->PushCFunction(EnableCustomRendering);
        LUA->SetField(-2, "EnableCustomRendering");
        
        LUA->PushCFunction(DisableCustomRendering);
        LUA->SetField(-2, "DisableCustomRendering");
        
        LUA->PushCFunction(RebuildMeshes);
        LUA->SetField(-2, "RebuildMeshes");
        
        // Statistics/Debug
        LUA->PushCFunction(GetRenderStats);
        LUA->SetField(-2, "GetRenderStats");
        
        LUA->PushCFunction(GetTotalVertexCount);
        LUA->SetField(-2, "GetTotalVertexCount");
        
        LUA->PushCFunction(GetChunkCount);
        LUA->SetField(-2, "GetChunkCount");
        
        LUA->PushCFunction(GetDrawCalls);
        LUA->SetField(-2, "GetDrawCalls");
        
        // Utility functions
        LUA->PushCFunction(IsMeshSystemEnabled);
        LUA->SetField(-2, "IsMeshSystemEnabled");
        
        LUA->PushCFunction(GetMaterialCount);
        LUA->SetField(-2, "GetMaterialCount");
    
    // Create RTX table for configuration functions
    LUA->CreateTable();
        LUA->PushString("RTX Mesh System"); // Table metavalue
        LUA->SetField(-2, "__type");
        
        // Configuration functions
        LUA->PushCFunction(SetChunkSize);
        LUA->SetField(-2, "SetChunkSize");
        
        LUA->PushCFunction(GetChunkSize);
        LUA->SetField(-2, "GetChunkSize");
        
        LUA->PushCFunction(SetDebugMode);
        LUA->SetField(-2, "SetDebugMode");
        
        LUA->PushCFunction(GetDebugMode);
        LUA->SetField(-2, "GetDebugMode");
        
        // Advanced configuration
        LUA->PushCFunction(SetMaxVerticesPerChunk);
        LUA->SetField(-2, "SetMaxVerticesPerChunk");
        
    LUA->SetField(-2, "RTX");
    
    LUA->Pop(); // Pop global table
    
    Msg("[Mesh System] Lua bindings initialized successfully\n");
}

LUA_FUNCTION(EnableCustomRendering) {
    try {
        if (MeshManager::Instance().IsEnabled()) {
            LUA->PushBool(true);
            return 1;
        }

        bool success = MeshManager::Instance().Initialize();
        if (success) {
            // Disable default world rendering
            ConVar* r_drawworld = cvar->FindVar("r_drawworld");
            if (r_drawworld) {
                r_drawworld->SetValue(0);
            }
        }

        LUA->PushBool(success);
        return 1;
    }
    catch (const std::exception& e) {
        Msg("[Mesh System] Exception in EnableCustomRendering: %s\n", e.what());
        LUA->PushBool(false);
        return 1;
    }
}

LUA_FUNCTION(DisableCustomRendering) {
    try {
        if (!MeshManager::Instance().IsEnabled()) {
            LUA->PushBool(true);
            return 1;
        }

        // Restore default world rendering
        ConVar* r_drawworld = cvar->FindVar("r_drawworld");
        if (r_drawworld) {
            r_drawworld->SetValue(1);
        }

        MeshManager::Instance().Shutdown();
        LUA->PushBool(true);
        return 1;
    }
    catch (const std::exception& e) {
        Msg("[Mesh System] Exception in DisableCustomRendering: %s\n", e.what());
        LUA->PushBool(false);
        return 1;
    }
}

LUA_FUNCTION(RebuildMeshes) {
    try {
        if (!MeshManager::Instance().IsEnabled()) {
            LUA->ThrowError("Mesh system is not enabled");
            return 0;
        }

        double startTime = Plat_FloatTime();
        MeshManager::Instance().RebuildMeshes();
        double duration = Plat_FloatTime() - startTime;

        // Return build time and success status
        LUA->PushBool(true);
        LUA->PushNumber(duration);
        return 2;
    }
    catch (const std::exception& e) {
        Msg("[Mesh System] Exception in RebuildMeshes: %s\n", e.what());
        LUA->PushBool(false);
        LUA->PushString(e.what());
        return 2;
    }
}

LUA_FUNCTION(GetRenderStats) {
    auto& manager = MeshManager::Instance();
    if (!manager.IsEnabled()) {
        LUA->PushNil();
        return 1;
    }

    const auto& stats = manager.GetRenderStats();
    
    LUA->CreateTable();
        // Core statistics
        LUA->PushNumber(stats.drawCalls);
        LUA->SetField(-2, "draws");
        
        LUA->PushNumber(stats.materialChanges);
        LUA->SetField(-2, "materialChanges");
        
        LUA->PushNumber(stats.totalVertices);
        LUA->SetField(-2, "vertices");
        
        LUA->PushNumber(stats.activeChunks);
        LUA->SetField(-2, "chunks");
        
        // Detailed chunk info
        LUA->CreateTable();
            LUA->PushNumber(manager.GetOpaqueChunkCount());
            LUA->SetField(-2, "opaque");
            
            LUA->PushNumber(manager.GetTranslucentChunkCount());
            LUA->SetField(-2, "translucent");
        LUA->SetField(-2, "chunkCounts");
        
        // Performance metrics
        LUA->PushNumber(stats.lastBuildTime);
        LUA->SetField(-2, "buildTime");
        
        LUA->PushNumber(stats.lastFrameTime);
        LUA->SetField(-2, "frameTime");
    
    return 1;
}

LUA_FUNCTION(GetTotalVertexCount) {
    if (!MeshManager::Instance().IsEnabled()) {
        LUA->PushNumber(0);
        return 1;
    }
    
    LUA->PushNumber(MeshManager::Instance().GetRenderStats().totalVertices);
    return 1;
}

LUA_FUNCTION(GetChunkCount) {
    if (!MeshManager::Instance().IsEnabled()) {
        LUA->PushNumber(0);
        return 1;
    }
    
    LUA->PushNumber(MeshManager::Instance().GetRenderStats().activeChunks);
    return 1;
}

LUA_FUNCTION(GetDrawCalls) {
    if (!MeshManager::Instance().IsEnabled()) {
        LUA->PushNumber(0);
        return 1;
    }
    
    LUA->PushNumber(MeshManager::Instance().GetRenderStats().drawCalls);
    return 1;
}

LUA_FUNCTION(IsMeshSystemEnabled) {
    LUA->PushBool(MeshManager::Instance().IsEnabled());
    return 1;
}

LUA_FUNCTION(GetMaterialCount) {
    if (!MeshManager::Instance().IsEnabled()) {
        LUA->PushNumber(0);
        return 1;
    }
    
    LUA->PushNumber(MeshManager::Instance().GetMaterialCount());
    return 1;
}

// Configuration functions
LUA_FUNCTION(SetChunkSize) {
    if (!LUA->IsType(1, GarrysMod::Lua::Type::NUMBER)) {
        LUA->ThrowError("Argument must be a number");
        return 0;
    }
    
    int size = static_cast<int>(LUA->GetNumber(1));
    if (size < 64 || size > 8192) {
        LUA->ThrowError("Chunk size must be between 64 and 8192");
        return 0;
    }
    
    ConVar* chunkSize = cvar->FindVar("rtx_chunk_size");
    if (chunkSize) {
        chunkSize->SetValue(size);
        
        // Trigger rebuild if enabled
        if (MeshManager::Instance().IsEnabled()) {
            MeshManager::Instance().RebuildMeshes();
        }
        
        LUA->PushBool(true);
    }
    else {
        LUA->PushBool(false);
    }
    return 1;
}

LUA_FUNCTION(GetChunkSize) {
    ConVar* chunkSize = cvar->FindVar("rtx_chunk_size");
    if (chunkSize) {
        LUA->PushNumber(chunkSize->GetInt());
    }
    else {
        LUA->PushNumber(512); // Default value
    }
    return 1;
}

LUA_FUNCTION(SetDebugMode) {
    if (!LUA->IsType(1, GarrysMod::Lua::Type::BOOL)) {
        LUA->ThrowError("Argument must be a boolean");
        return 0;
    }
    
    bool debug = LUA->GetBool(1);
    ConVar* debugMode = cvar->FindVar("rtx_force_render_debug");
    if (debugMode) {
        debugMode->SetValue(debug ? 1 : 0);
        LUA->PushBool(true);
    }
    else {
        LUA->PushBool(false);
    }
    return 1;
}

LUA_FUNCTION(GetDebugMode) {
    ConVar* debugMode = cvar->FindVar("rtx_force_render_debug");
    if (debugMode) {
        LUA->PushBool(debugMode->GetBool());
    }
    else {
        LUA->PushBool(false);
    }
    return 1;
}

LUA_FUNCTION(SetMaxVerticesPerChunk) {
    if (!LUA->IsType(1, GarrysMod::Lua::Type::NUMBER)) {
        LUA->ThrowError("Argument must be a number");
        return 0;
    }
    
    int maxVerts = static_cast<int>(LUA->GetNumber(1));
    if (maxVerts < 1000 || maxVerts > 32768) {
        LUA->ThrowError("Max vertices must be between 1000 and 32768");
        return 0;
    }
    
    if (MeshManager::Instance().IsEnabled()) {
        MeshManager::Instance().SetMaxVerticesPerChunk(maxVerts);
        MeshManager::Instance().RebuildMeshes();
    }
    
    LUA->PushBool(true);
    return 1;
}

} // namespace MeshSystemLua