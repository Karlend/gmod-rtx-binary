// source/mesh_system/mesh_manager.h
#pragma once
#include "GarrysMod/Lua/Interface.h"
#include "mathlib/vector.h"
#include "materialsystem/imaterialsystem.h"
#include <unordered_map>
#include <vector>
#include <string>
#include "cdll_int.h"
#include "icvar.h"
#include "materialsystem/imaterialsystem.h"
#include "eiface.h"
#include "icliententitylist.h"
#include "debugoverlay_shared.h"
#include "materialsystem/ishaderapi.h"

#define SHADER_BLEND_SRC_ALPHA 4
#define SHADER_BLEND_ONE_MINUS_SRC_ALPHA 5
#define SHADER_STENCILFUNC_ALWAYS 1
#define SHADER_STENCILOP_KEEP 1

class MeshChunk;
class BSPReader;
class IClientEntity;

class MeshManager {
public:
    struct Config {
        bool enabled = false;
        bool debugMode = false;
        int chunkSize = 512;
        int maxVerticesPerMesh = 10000;
    };

    // Debug/stats
    struct RenderStats {
        int drawCalls = 0;
        int materialChanges = 0;
        int totalVertices = 0;
        int activeChunks = 0;
        float lastBuildTime = 0.0f;
        float lastFrameTime = 0.0f;
    };

    static MeshManager& Instance();

    void Initialize();
    void Shutdown();
    void RebuildMeshes();
    void DrawDebugInfo();

    bool IsEnabled() const { return m_config.enabled; }
    size_t GetMaterialCount() const { return m_materials.size(); }
    size_t GetOpaqueChunkCount() const { return m_opaqueChunks.size(); }
    size_t GetTranslucentChunkCount() const { return m_translucentChunks.size(); }
    void SetMaxVerticesPerChunk(int maxVerts) { m_config.maxVerticesPerMesh = maxVerts; }
    const RenderStats& GetRenderStats() const { return m_stats; }
    
    // Render passes
    void RenderOpaqueChunks();
    void RenderTranslucentChunks();

    // Lua interface
    void RegisterLuaFunctions(GarrysMod::Lua::ILuaBase* LUA);

private:
    // Chunk storage
    std::vector<IMaterial*> m_materials;
    RenderStats m_stats;
    std::unordered_map<std::string, std::vector<MeshChunk>> m_opaqueChunks;
    std::unordered_map<std::string, std::vector<MeshChunk>> m_translucentChunks;
    BSPReader* m_bspReader;
    Config m_config;

    MeshManager();
    ~MeshManager();

    // Helper function to get current camera position
    bool UpdateCameraPosition();

    // Mesh building
    void ProcessMapGeometry();
    void CleanupMeshes();
    std::string GetChunkKey(const Vector& pos) const;

    void LogDebug(const char* format, ...);
};