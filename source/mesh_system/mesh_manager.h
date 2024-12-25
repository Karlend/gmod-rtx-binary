// source/mesh_system/mesh_manager.h
#pragma once
#include "GarrysMod/Lua/Interface.h"
#include "mathlib/vector.h"
#include "materialsystem/imaterialsystem.h"
#include <unordered_map>
#include <vector>
#include <string>

class MeshChunk;
class BSPReader;

class MeshManager {
public:
    static MeshManager& Instance();

    void Initialize();
    void Shutdown();
    void RebuildMeshes();
    
    // Render passes
    void RenderOpaqueChunks();
    void RenderTranslucentChunks();

    // Debug/stats
    struct RenderStats {
        int drawCalls = 0;
        int materialChanges = 0;
        int totalVertices = 0;
        int activeChunks = 0;
    };

    // Lua interface
    void RegisterLuaFunctions(GarrysMod::Lua::ILuaBase* LUA);

private:
    MeshManager();
    ~MeshManager();

    // Helper function to get current camera position
    bool UpdateCameraPosition();

    // Mesh building
    void ProcessMapGeometry();
    void CleanupMeshes();
    std::string GetChunkKey(const Vector& pos) const;
    
    // Configuration
    struct Config {
        bool enabled = false;
        bool debugMode = false;
        int chunkSize = 512;
        int maxVerticesPerMesh = 10000;
    } m_config;

    // Chunk storage
    std::unordered_map<std::string, std::vector<MeshChunk>> m_opaqueChunks;
    std::unordered_map<std::string, std::vector<MeshChunk>> m_translucentChunks;
    
    // Support systems
    BSPReader* m_bspReader;
    RenderStats m_stats;

    void LogDebug(const char* format, ...);
};