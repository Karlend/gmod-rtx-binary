// source/rtx_meshes/rtx_mesh_manager.cpp
#include "rtx_mesh_manager.h"
#include <tier0/dbg.h>
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "cdll_int.h"
#include "meshutils.h"

static int RebuildMeshesLua(lua_State* L)
{
    RTXMeshManager::Instance().RebuildMeshes();
    return 0;
}

// External engine interfaces we need
extern IMaterialSystem* materials;
extern IVEngineClient* engine;

RTXMeshManager& RTXMeshManager::Instance() {
    static RTXMeshManager instance;
    return instance;
}

void RTXMeshManager::Initialize() {
    if (m_isEnabled) return;
    
    m_isEnabled = true;
    RebuildMeshes();
    
    Msg("[RTX Mesh Manager] Initialized\n");
}

void RTXMeshManager::Shutdown() {
    if (!m_isEnabled) return;
    
    CleanupMeshes();
    m_isEnabled = false;
    
    Msg("[RTX Mesh Manager] Shutdown\n");
}

void RTXMeshManager::RebuildMeshes() {
    if (!m_isEnabled || !materials) {
        Warning("[RTX Mesh Manager] Cannot rebuild meshes - interfaces not available\n");
        return;
    }

    CleanupMeshes();
    ProcessMapGeometry();
}

void RTXMeshManager::ProcessMapGeometry() {
    IMatRenderContext* pRenderContext = materials->GetRenderContext();
    if (!pRenderContext) {
        Warning("[RTX Mesh Manager] Cannot get render context\n");
        return;
    }

    Msg("[RTX Mesh Manager] Beginning map geometry processing...\n");
    
    // Create mesh for collecting geometry
    IMesh* pMesh = pRenderContext->GetDynamicMesh(true);
    if (!pMesh) {
        Warning("[RTX Mesh Manager] Failed to create dynamic mesh\n");
        return;
    }

    // Begin mesh building
    CMeshBuilder meshBuilder;
    meshBuilder.Begin(pMesh, MATERIAL_TRIANGLES, 1024); // Start with reasonable size

    size_t totalVertices = 0;
    size_t totalFaces = 0;

    // Process geometry in chunks
    while (meshBuilder.VertexCount() < 1024) { // Use fixed max vertex count
        
        // Add vertices to current chunk
        meshBuilder.Position3f(0, 0, 0);
        meshBuilder.Normal3f(0, 1, 0);
        meshBuilder.Color4ub(255, 255, 255, 255);
        meshBuilder.TexCoord2f(0, 0, 0);
        meshBuilder.AdvanceVertex();

        totalVertices++;
    }

    meshBuilder.End();
    
    // Store chunk
    MeshChunk chunk;
    chunk.vertices.resize(totalVertices);
    for (size_t i = 0; i < totalVertices; i++) {
        // Copy vertex data from mesh
        const float* pos = meshBuilder.Position();
        const float* norm = meshBuilder.Normal();
        unsigned int color = meshBuilder.Color();
        const float* texcoord = meshBuilder.TexCoord(0);

        chunk.vertices[i].pos = Vector(pos[0], pos[1], pos[2]); 
        chunk.vertices[i].normal = Vector(norm[0], norm[1], norm[2]);
        // etc...

        meshBuilder.SelectVertex(i);
    }

    // Add to appropriate chunk map
    ChunkKey key = GetChunkKey(Vector(0,0,0));
    m_opaqueChunks[key].push_back(chunk);

    Msg("[RTX Mesh Manager] Processed %d faces with %d vertices\n", 
        totalFaces, totalVertices);
}

RTXMeshManager::ChunkKey RTXMeshManager::GetChunkKey(const Vector& pos) const {
    return {
        static_cast<int>(floor(pos.x / m_chunkSize)),
        static_cast<int>(floor(pos.y / m_chunkSize)), 
        static_cast<int>(floor(pos.z / m_chunkSize))
    };
}

void RTXMeshManager::RenderOpaqueChunks() {
    if (!m_isEnabled || !materials) return;

    IMatRenderContext* pRenderContext = materials->GetRenderContext();
    if (!pRenderContext) return;

    for (const auto& chunkPair : m_opaqueChunks) {
        const auto& chunks = chunkPair.second;
        
        for (const auto& chunk : chunks) {
            if (chunk.vertices.empty()) continue;
            
            IMesh* pMesh = pRenderContext->GetDynamicMesh();
            if (!pMesh) continue;

            CMeshBuilder meshBuilder;
            meshBuilder.Begin(pMesh, MATERIAL_TRIANGLES, chunk.vertices.size());

            for (const auto& vertex : chunk.vertices) {
                meshBuilder.Position3f(vertex.pos.x, vertex.pos.y, vertex.pos.z);
                meshBuilder.Normal3f(vertex.normal.x, vertex.normal.y, vertex.normal.z);
                // etc...
                meshBuilder.AdvanceVertex();
            }

            meshBuilder.End();
            pMesh->Draw();
        }
    }
}

// Lua interface functions
LUA_FUNCTION(EnableCustomRendering) {
    auto& manager = RTXMeshManager::Instance();
    bool enable = LUA->GetBool(1);
    
    if (enable) {
        manager.Initialize();
    } else {
        manager.Shutdown();
    }
    
    return 0;
}

LUA_FUNCTION(RebuildMeshes) {
    RTXMeshManager::Instance().RebuildMeshes();
    return 0;
}

void RTXMeshManager::RegisterLuaFunctions(GarrysMod::Lua::ILuaBase* LUA) {
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    
    LUA->PushCFunction(EnableCustomRendering);
    LUA->SetField(-2, "EnableCustomRendering");
    
    // Create a static function for Lua to call
    LUA->PushCFunction(RebuildMeshesLua);  // Use the static function instead of lambda
    LUA->SetField(-2, "RebuildMeshes");
    
    LUA->Pop();
}

void RTXMeshManager::CleanupMeshes() {
    m_opaqueChunks.clear();
    m_translucentChunks.clear();
}

void RTXMeshManager::LogMessage(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Msg("[RTX Mesh Manager] %s", buffer);
}