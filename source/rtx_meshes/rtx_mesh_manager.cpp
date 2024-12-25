// source/rtx_meshes/rtx_mesh_manager.cpp
#include "rtx_mesh_manager.h"
#include <tier0/dbg.h>
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "cdll_int.h"

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
    
    Msg("[RTX Mesh] Initialize called\n");

    // Validate interfaces
    if (!materials) {
        Warning("[RTX Mesh] Cannot initialize - MaterialSystem interface not available\n");
        return;
    }

    // Verify we can get a render context
    IMatRenderContext* pRenderContext = materials->GetRenderContext();
    if (!pRenderContext) {
        Warning("[RTX Mesh] Cannot initialize - Failed to get render context\n");
        return;
    }

    // Test creating a mesh
    IMesh* pTestMesh = pRenderContext->GetDynamicMesh();
    if (!pTestMesh) {
        Warning("[RTX Mesh] Cannot initialize - Failed to create test mesh\n");
        return;
    }

    Msg("[RTX Mesh] Successfully validated interfaces and mesh creation\n");
    
    m_isEnabled = true;
    RebuildMeshes();
    
    Msg("[RTX Mesh] Initialized with %d chunks\n", m_opaqueChunks.size());
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
    if (!materials) {
        Warning("[RTX Mesh] Cannot process geometry - MaterialSystem interface not available\n");
        return;
    }

    IMatRenderContext* pRenderContext = materials->GetRenderContext();
    if (!pRenderContext) {
        Warning("[RTX Mesh] Cannot get render context\n");
        return;
    }

    // Create a basic material for testing
    IMaterial* testMaterial = materials->FindMaterial("debug/debugvertexcolor", TEXTURE_GROUP_MODEL);
    if (!testMaterial) {
        Warning("[RTX Mesh] Failed to find test material\n");
        return;
    }

    Msg("[RTX Mesh] Beginning map geometry processing...\n");
    
    // For testing, create a simple quad
    MeshChunk chunk;
    chunk.vertices.resize(4);
    chunk.material = testMaterial;

    // Get player position for positioning the quad
    Vector playerPos(0, 0, 0);
    if (engine && engine->IsInGame()) {
        playerPos = engine->GetViewAngles();
        playerPos.z += 64; // Place slightly above eye level
    }

    // Create a quad facing the player
    float size = 32.0f; // Smaller size for testing
    chunk.vertices[0].pos = playerPos + Vector(-size, -size, 0);
    chunk.vertices[1].pos = playerPos + Vector( size, -size, 0);
    chunk.vertices[2].pos = playerPos + Vector( size,  size, 0);
    chunk.vertices[3].pos = playerPos + Vector(-size,  size, 0);

    // Set texture coordinates
    chunk.vertices[0].uv = Vector2D(0, 0);
    chunk.vertices[1].uv = Vector2D(1, 0);
    chunk.vertices[2].uv = Vector2D(1, 1);
    chunk.vertices[3].uv = Vector2D(0, 1);

    // Set normals and colors - make it bright red for visibility
    for (auto& vert : chunk.vertices) {
        vert.normal = Vector(0, 0, 1);
        vert.color[0] = 255;  // R
        vert.color[1] = 0;    // G
        vert.color[2] = 0;    // B
        vert.color[3] = 255;  // A
    }

    // Add indices for two triangles
    chunk.indices = { 0, 1, 2, 0, 2, 3 };

    // Add to chunks
    ChunkKey key = GetChunkKey(Vector(0,0,0));
    m_opaqueChunks[key].push_back(chunk);

    Msg("[RTX Mesh] Created test quad with %d vertices and %d indices\n", 
        chunk.vertices.size(), chunk.indices.size());
}

RTXMeshManager::ChunkKey RTXMeshManager::GetChunkKey(const Vector& pos) const {
    return {
        static_cast<int>(floor(pos.x / m_chunkSize)),
        static_cast<int>(floor(pos.y / m_chunkSize)), 
        static_cast<int>(floor(pos.z / m_chunkSize))
    };
}

void RTXMeshManager::RenderOpaqueChunks() {
    if (!m_isEnabled || !materials) {
        Warning("[RTX Mesh] RenderOpaqueChunks called but system not ready (enabled: %d, materials: %p)\n", 
            m_isEnabled, materials);
        return;
    }

    IMatRenderContext* pRenderContext = materials->GetRenderContext();
    if (!pRenderContext) {
        Warning("[RTX Mesh] Failed to get render context\n");
        return;
    }

    int chunksRendered = 0;
    int verticesRendered = 0;

    for (const auto& chunkPair : m_opaqueChunks) {
        const auto& chunks = chunkPair.second;
        
        for (const auto& chunk : chunks) {
            if (chunk.vertices.empty()) continue;
            
            // Bind the material
            if (!chunk.material) {
                Warning("[RTX Mesh] Chunk has no material\n");
                continue;
            }
            pRenderContext->Bind(chunk.material);
            
            IMesh* pMesh = pRenderContext->GetDynamicMesh(true);
            if (!pMesh) {
                Warning("[RTX Mesh] Failed to get dynamic mesh\n");
                continue;
            }

            CMeshBuilder meshBuilder;
            meshBuilder.Begin(pMesh, MATERIAL_TRIANGLES, chunk.vertices.size(), chunk.indices.size());

            // Add vertices
            for (const auto& vertex : chunk.vertices) {
                meshBuilder.Position3f(vertex.pos.x, vertex.pos.y, vertex.pos.z);
                meshBuilder.Normal3f(vertex.normal.x, vertex.normal.y, vertex.normal.z);
                meshBuilder.Color4ub(vertex.color[0], vertex.color[1], vertex.color[2], vertex.color[3]);
                meshBuilder.TexCoord2f(0, vertex.uv.x, vertex.uv.y);
                meshBuilder.AdvanceVertex();
                verticesRendered++;
            }

            // Add indices
            for (unsigned short index : chunk.indices) {
                meshBuilder.Index(index);
                meshBuilder.AdvanceIndex();
            }

            meshBuilder.End();
            pMesh->Draw();
            chunksRendered++;
        }
    }

    Msg("[RTX Mesh] Rendered %d chunks with %d vertices\n", chunksRendered, verticesRendered);
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