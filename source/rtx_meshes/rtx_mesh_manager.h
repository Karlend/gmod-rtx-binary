#pragma once
#include <vector>
#include <unordered_map>
#include "GarrysMod/Lua/Interface.h"
#include "materialsystem/imaterial.h"
#include "mathlib/vector.h"
#include "vstdlib/random.h"

class RTXMeshManager {
public:
    static RTXMeshManager& Instance();

    void Initialize();
    void Shutdown();
    void RebuildMeshes();
    void RenderOpaqueChunks();
    void RenderTranslucentChunks();

    // Lua interface functions
    static void RegisterLuaFunctions(GarrysMod::Lua::ILuaBase* LUA);

private:
    RTXMeshManager() : m_chunkSize(512), m_isEnabled(false) {}
    ~RTXMeshManager() { Shutdown(); }

    struct Vertex {
        Vector pos;
        Vector normal;
        Vector2D uv;
        unsigned char color[4];
    };

    struct ChunkKey {
        int x, y, z;
        
        bool operator==(const ChunkKey& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    // Add hash function for ChunkKey
    struct ChunkKeyHash {
        std::size_t operator()(const ChunkKey& key) const {
            return std::hash<int>()(key.x) ^
                   (std::hash<int>()(key.y) << 1) ^
                   (std::hash<int>()(key.z) << 2);
        }
    };

    struct MeshChunk {
        std::vector<Vertex> vertices;
        std::vector<unsigned short> indices;
        IMaterial* material;
    };


    // Use ChunkKeyHash in unordered_map
    std::unordered_map<ChunkKey, std::vector<MeshChunk>, ChunkKeyHash> m_opaqueChunks;
    std::unordered_map<ChunkKey, std::vector<MeshChunk>, ChunkKeyHash> m_translucentChunks;
    
    void ProcessMapGeometry();
    ChunkKey GetChunkKey(const Vector& pos) const;
    void CreateOptimizedMeshes();
    void CleanupMeshes();

    int m_chunkSize;
    bool m_isEnabled;

    void LogMessage(const char* format, ...);
};