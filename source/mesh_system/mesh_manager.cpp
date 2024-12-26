// source/mesh_system/mesh_manager.cpp
#include "mesh_manager.h"
#include "mesh_chunk.h"
#include "bsp_reader.h"
#include "tier0/dbg.h"
#include <algorithm>
#include "materialsystem/ishaderapi.h"
#include "debugoverlay.h"
#include "cdll_int.h"
#include "icliententitylist.h"
#include "utils/shader_types.h"

#define SHADER_BLEND_SRC_ALPHA 4
#define SHADER_BLEND_ONE_MINUS_SRC_ALPHA 5
#define MAX_LIGHTMAPS 4

// External engine interfaces
extern IMaterialSystem* g_materials;
extern IVEngineClient* g_engine;
extern IVModelInfo* g_modelinfo;
extern IClientEntityList* g_entitylist;

MeshManager& MeshManager::Instance() {
    static MeshManager instance;
    return instance;
}

MeshManager::MeshManager() 
    : m_bspReader(nullptr) {
    // Replace RegisterConVar with CreateConVar
    ConVar* rtx_force_render = new ConVar("rtx_force_render", "1", FCVAR_CLIENTDLL, "Forces custom mesh rendering of map");
    ConVar* rtx_force_render_debug = new ConVar("rtx_force_render_debug", "0", FCVAR_CLIENTDLL, "Shows debug info for mesh rendering");
    ConVar* rtx_chunk_size = new ConVar("rtx_chunk_size", "512", FCVAR_CLIENTDLL, "Size of chunks for mesh combining");
}

MeshManager::~MeshManager() {
    Shutdown();
}

void MeshManager::Initialize() {
    if (m_config.enabled) return;
    
    Msg("[Mesh System] Initializing...\n");

    // Validate required interfaces
    if (!g_materials || !g_engine) {
        Warning("[Mesh System] Cannot initialize - Missing required interfaces\n");
        return;
    }

    // Initialize BSP reader
    if (!m_bspReader) {
        m_bspReader = new BSPReader();
    }

    // Read configuration from ConVars
    m_config.enabled = true;
    m_config.debugMode = cvar->FindVar("rtx_force_render_debug")->GetBool();
    m_config.chunkSize = cvar->FindVar("rtx_chunk_size")->GetInt();

    // Initial mesh build
    RebuildMeshes();
    
    if (m_config.debugMode) {
        Msg("[Mesh System] Initialized with chunk size %d\n", m_config.chunkSize);
    }
}

void MeshManager::Shutdown() {
    if (!m_config.enabled) return;

    CleanupMeshes();
    
    delete m_bspReader;
    m_bspReader = nullptr;
    
    m_config.enabled = false;
    
    Msg("[Mesh System] Shutdown complete\n");
}

void MeshManager::CleanupMeshes() {
    m_opaqueChunks.clear();
    m_translucentChunks.clear();
    
    // Reset stats
    m_stats = RenderStats();
}

std::string MeshManager::GetChunkKey(const Vector& pos) const {
    int x = static_cast<int>(floor(pos.x / m_config.chunkSize));
    int y = static_cast<int>(floor(pos.y / m_config.chunkSize));
    int z = static_cast<int>(floor(pos.z / m_config.chunkSize));
    
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d,%d,%d", x, y, z);
    return std::string(buffer);
}

void MeshManager::LogDebug(const char* format, ...) {
    if (!m_config.debugMode) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Msg("[Mesh System] %s", buffer);
}

void MeshManager::RebuildMeshes() {
    if (!m_config.enabled || !g_materials) {
        Warning("[Mesh System] Cannot rebuild meshes - system not initialized\n");
        return;
    }

    LogDebug("Building chunked meshes...\n");
    double startTime = Plat_FloatTime();
    
    CleanupMeshes();
    ProcessMapGeometry();

    double duration = Plat_FloatTime() - startTime;
    LogDebug("Built chunked meshes in %.2f seconds\n", duration);
    LogDebug("Total vertex count: %d\n", m_stats.totalVertices);
}

void MeshManager::ProcessMapGeometry() {
    if (!m_bspReader || !g_engine) {
        Warning("[Mesh System] Missing required interfaces for mesh processing\n");
        return;
    }

    // Get world model
    model_t* worldModel = g_modelinfo->GetModel(0); // 0 is always world model
    if (!worldModel) {
        Warning("[Mesh System] Failed to get world model\n");
        return;
    }

    // Load BSP data
    if (!m_bspReader->Load(worldModel)) {
        Warning("[Mesh System] Failed to load BSP data\n");
        return;
    }

    // Temporary storage for sorting faces
    struct ChunkMaterialGroup {
        IMaterial* material;
        std::vector<BSPFace*> faces;
        bool isTranslucent;
    };
    
    std::unordered_map<std::string, // Chunk key
        std::unordered_map<std::string, // Material name
            ChunkMaterialGroup>> opaqueGroups, translucentGroups;

    // Process each leaf and sort faces into chunks
    for (int i = 0; i < m_bspReader->NumLeafs(); i++) {
        BSPLeaf* leaf = m_bspReader->GetLeaf(i);
        if (!leaf || leaf->IsOutsideMap()) continue;

        for (int j = 0; j < leaf->NumFaces(); j++) {
            BSPFace* face = leaf->GetFace(j);
            if (!face || !face->ShouldRender()) continue;

            // Skip skybox faces
            IMaterial* material = face->GetMaterial();
            if (!material || IsSkyboxMaterial(material)) continue;

            // Get face center for chunk assignment
            Vector center;
            if (!face->GetCenter(center)) continue;

            // Determine chunk and material keys
            std::string chunkKey = GetChunkKey(center);
            std::string materialName = material->GetName();

            // Sort into appropriate chunk group
            auto& targetGroups = face->IsTranslucent() ? translucentGroups : opaqueGroups;
            auto& materialGroup = targetGroups[chunkKey][materialName];

            if (materialGroup.faces.empty()) {
                materialGroup.material = material;
                materialGroup.isTranslucent = face->IsTranslucent();
                material->IncrementReferenceCount(); // Keep material alive
            }

            materialGroup.faces.push_back(face);
        }
    }

    // Create meshes from sorted faces
    auto processFaceGroups = [this](
        const std::unordered_map<std::string, 
        std::unordered_map<std::string, ChunkMaterialGroup>>& groups,
        std::unordered_map<std::string, std::vector<MeshChunk>>& targetChunks) {
        
        for (const auto& chunkPair : groups) {
            const std::string& chunkKey = chunkPair.first;
            
            for (const auto& materialPair : chunkPair.second) {
                const ChunkMaterialGroup& group = materialPair.second;
                
                if (group.faces.empty()) continue;

                MeshChunk chunk(group.material);
                std::vector<Vertex> vertices;
                std::vector<unsigned short> indices;

                // Collect all vertices and indices from faces
                for (BSPFace* face : group.faces) {
                    size_t baseVertex = vertices.size();
                    
                    std::vector<Vertex> faceVerts;
                    std::vector<unsigned short> faceIndices;
                    
                    if (face->GetVertexData(faceVerts, faceIndices)) {
                        // Adjust indices for the base vertex
                        for (unsigned short& idx : faceIndices) {
                            idx += baseVertex;
                        }
                        
                        vertices.insert(vertices.end(), faceVerts.begin(), faceVerts.end());
                        indices.insert(indices.end(), faceIndices.begin(), faceIndices.end());
                        
                        m_stats.totalVertices += faceVerts.size();
                    }
                }

                // Create chunks with vertex limit
                size_t vertexOffset = 0;
                while (vertexOffset < vertices.size()) {
                    size_t remainingVerts = vertices.size() - vertexOffset;
                    size_t vertsThisChunk = std::min(remainingVerts, 
                        static_cast<size_t>(m_config.maxVerticesPerMesh));

                    MeshChunk newChunk(group.material);
                    std::vector<Vertex> chunkVerts(
                        vertices.begin() + vertexOffset,
                        vertices.begin() + vertexOffset + vertsThisChunk);

                    // Find indices that reference vertices in this chunk
                    std::vector<unsigned short> chunkIndices;
                    for (size_t i = 0; i < indices.size(); i += 3) {
                        if (indices[i] >= vertexOffset && 
                            indices[i] < vertexOffset + vertsThisChunk) {
                            // Adjust indices for this chunk
                            chunkIndices.push_back(indices[i] - vertexOffset);
                            chunkIndices.push_back(indices[i + 1] - vertexOffset);
                            chunkIndices.push_back(indices[i + 2] - vertexOffset);
                        }
                    }

                    if (newChunk.AddFace(chunkVerts, chunkIndices)) {
                        targetChunks[chunkKey].push_back(std::move(newChunk));
                    }

                    vertexOffset += vertsThisChunk;
                }
            }
        }
    };

    // Process both opaque and translucent groups
    processFaceGroups(opaqueGroups, m_opaqueChunks);
    processFaceGroups(translucentGroups, m_translucentChunks);

    // Cleanup
    m_bspReader->Unload();

    LogDebug("Created %zu opaque chunks and %zu translucent chunks\n",
        m_opaqueChunks.size(), m_translucentChunks.size());
}

bool MeshManager::UpdateCameraPosition() {
    if (!g_engine || !g_entitylist) return false;

    Vector cameraPos;
    QAngle viewAngles;
    if (g_engine->GetViewAngles(viewAngles)) {
        if (IClientEntity* localPlayer = g_entitylist->GetClientEntity(g_engine->GetLocalPlayer())) {
            Vector viewOffset;
            if (localPlayer->GetAbsOrigin(&cameraPos) && localPlayer->GetViewOffset(&viewOffset)) {
                cameraPos += viewOffset;
                return true;
            }
        }
    }
    return false;
}

bool MeshManager::IsSkyboxMaterial(IMaterial* material) {
    if (!material) return false;
    
    const char* matName = material->GetName();
    if (!matName) return false;
    
    // Convert to lowercase for comparison
    char lowerName[256];
    Q_strncpy(lowerName, matName, sizeof(lowerName));
    Q_strlower(lowerName);
    
    return strstr(lowerName, "tools/toolsskybox") ||
           strstr(lowerName, "skybox/") ||
           strstr(lowerName, "sky_");
}

void MeshManager::RenderOpaqueChunks() {
    if (!m_config.enabled || !g_materials) return;

    // Update camera position
    UpdateCameraPosition();

    IMatRenderContext* pRenderContext = g_materials->GetRenderContext();
    if (!pRenderContext) return;

    // Reset stats for this frame
    m_stats.drawCalls = 0;
    m_stats.materialChanges = 0;

    // Save render state
    pRenderContext->PushRenderTargetAndViewport();

    // Set up render state for opaque geometry
    pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
    pRenderContext->SetAmbientLight(1.0f, 1.0f, 1.0f);
    pRenderContext->FogMode(MATERIAL_FOG_LINEAR);

    // Current material tracking for state optimization
    IMaterial* currentMaterial = nullptr;

    // Render all opaque chunks
    for (const auto& chunkPair : m_opaqueChunks) {
        const auto& chunks = chunkPair.second;
        
        for (const auto& chunk : chunks) {
            if (!chunk.IsValid()) continue;

            // Material state change if needed
            IMaterial* material = chunk.GetMaterial();
            if (material != currentMaterial) {
                pRenderContext->Bind(material);
                currentMaterial = material;
                m_stats.materialChanges++;
            }

            // Draw the chunk
            chunk.Draw();
            m_stats.drawCalls++;
        }
    }

    // Restore render state
    pRenderContext->PopRenderTargetAndViewport();

    // Debug output
    if (m_config.debugMode) {
        static int frameCount = 0;
        if (++frameCount % 60 == 0) { // Log every 60 frames
            LogDebug("Opaque render stats - Draws: %d, Material changes: %d\n",
                m_stats.drawCalls, m_stats.materialChanges);
        }
    }
}

void MeshManager::RenderTranslucentChunks() {
    if (!m_config.enabled || !g_materials) return;

    // Update camera position
    UpdateCameraPosition();

    IMatRenderContext* pRenderContext = g_materials->GetRenderContext();
    if (!pRenderContext) return;

    // Track separate stats for translucent pass
    int translucentDraws = 0;
    int translucentMaterialChanges = 0;

    // Save render state
    pRenderContext->PushRenderTargetAndViewport();

    // Set up render state for translucent geometry
    pRenderContext->CullMode(MATERIAL_CULLMODE_CCW);
    pRenderContext->SetAmbientLight(1.0f, 1.0f, 1.0f);
    pRenderContext->FogMode(MATERIAL_FOG_LINEAR);
    
    // Set up translucent rendering state
    ShaderStencilState_t stencilState;
    stencilState.m_bEnable = false;
    stencilState.m_nReferenceValue = 0;
    stencilState.m_nTestMask = 0xFF;
    stencilState.m_nWriteMask = 0;
    stencilState.m_CompareFunc = SHADER_STENCILFUNC_ALWAYS;
    stencilState.m_PassOp = SHADER_STENCILOP_KEEP;
    stencilState.m_FailOp = SHADER_STENCILOP_KEEP;
    stencilState.m_ZFailOp = SHADER_STENCILOP_KEEP;

    pRenderContext->SetStencilState(stencilState);
    pRenderContext->OverrideDepthEnable(true, false);  // Enable depth test but disable writes
    
    // For each material, set up proper blend state
    if (material && material->IsTranslucent()) {
        pRenderContext->OverrideBlend(true, 
            SHADER_BLEND_SRC_ALPHA, 
            SHADER_BLEND_ONE_MINUS_SRC_ALPHA);
    }
    
    // Setup stencil state to prevent z-write
    ShaderStencilState_t stencilState;
    stencilState.m_bEnable = false;
    stencilState.m_nWriteMask = 0xFF;
    stencilState.m_nTestMask = 0xFF;
    stencilState.m_nReferenceValue = 0;
    stencilState.m_CompareFunc = SHADER_STENCILFUNC_ALWAYS;
    stencilState.m_PassOp = SHADER_STENCILOP_KEEP;
    stencilState.m_FailOp = SHADER_STENCILOP_KEEP;
    stencilState.m_ZFailOp = SHADER_STENCILOP_KEEP;
    pRenderContext->SetStencilState(stencilState);

    // Sort chunks by distance from camera for proper transparency
    struct SortedChunk {
        float distanceSqr;
        const MeshChunk* chunk;
    };
    std::vector<SortedChunk> sortedChunks;

    // Get camera position for sorting
    Vector cameraPos;
    QAngle viewAngles;
    if (g_engine->GetViewAngles(viewAngles)) {
        if (IClientEntity* localPlayer = g_entitylist->GetClientEntity(g_engine->GetLocalPlayer())) {
            Vector viewOffset;
            if (localPlayer->GetAbsOrigin(&cameraPos) && localPlayer->GetViewOffset(&viewOffset)) {
                cameraPos += viewOffset;
            }
        }
    }

    // Collect and sort translucent chunks
    for (const auto& chunkPair : m_translucentChunks) {
        const auto& chunks = chunkPair.second;
        for (const auto& chunk : chunks) {
            if (!chunk.IsValid()) continue;

            // Get chunk center (approximate)
            Vector chunkCenter = chunk.GetCenter();
            float distSqr = (chunkCenter - cameraPos).LengthSqr();

            sortedChunks.push_back({distSqr, &chunk});
        }
    }

    // Sort back to front
    std::sort(sortedChunks.begin(), sortedChunks.end(),
        [](const SortedChunk& a, const SortedChunk& b) {
            return a.distanceSqr > b.distanceSqr;
        });

    // Current material tracking
    IMaterial* currentMaterial = nullptr;

    // Setup material override state
    MaterialOverrideState_t overrideState;
    overrideState.m_bOverrideDepthWrite = true;
    overrideState.m_bOverrideAlphaWrite = true;
    overrideState.m_bEnableDepthWrite = false;
    overrideState.m_bEnableAlphaWrite = true;

    // Render sorted translucent chunks
    for (const auto& sorted : sortedChunks) {
        const MeshChunk* chunk = sorted.chunk;
        IMaterial* material = chunk->GetMaterial();

        // Material state change if needed
        if (material != currentMaterial) {
            pRenderContext->Bind(material);
            pRenderContext->SetMaterialOverrideState(overrideState);
            currentMaterial = material;
            translucentMaterialChanges++;
        }

        // Draw the chunk
        chunk->Draw();
        translucentDraws++;
    }

    // Restore render states
    pRenderContext->EnableAlpha(false);
    pRenderContext->SetStencilState(ShaderStencilState_t());  // Reset to default
    pRenderContext->OverrideDepthEnable(false, false);
    pRenderContext->OverrideBlend(false);
    
    // Clear material override
    MaterialOverrideState_t defaultOverride;
    pRenderContext->SetMaterialOverrideState(defaultOverride);
    
    pRenderContext->PopRenderTargetAndViewport();

    // Update overall stats
    m_stats.drawCalls += translucentDraws;
    m_stats.materialChanges += translucentMaterialChanges;

    // Debug output
    if (m_config.debugMode) {
        static int frameCount = 0;
        if (++frameCount % 60 == 0) {
            LogDebug("Translucent render stats - Draws: %d, Material changes: %d\n",
                translucentDraws, translucentMaterialChanges);
        }
    }
}

void MeshManager::DrawDebugInfo() {
    if (!m_config.debugMode) return;

    // Draw statistics
    int screenX = 10;
    int screenY = 10;
    debugoverlay->ScreenText(
        screenX, screenY,
        CFmtStr("Chunks: %d opaque, %d translucent", 
            m_opaqueChunks.size(), m_translucentChunks.size()),
        Color(255, 255, 255, 255), 0.0f);
}

// Helper function to render both passes
void MeshManager::RenderAll() {
    if (!m_config.enabled) return;

    // Render opaque geometry first
    RenderOpaqueChunks();

    // Then render translucent geometry
    RenderTranslucentChunks();

    // Total stats for this frame
    if (m_config.debugMode) {
        static int frameCount = 0;
        if (++frameCount % 60 == 0) {
            LogDebug("Total render stats - Draws: %d, Material changes: %d, Active chunks: %d\n",
                m_stats.drawCalls, m_stats.materialChanges, 
                m_opaqueChunks.size() + m_translucentChunks.size());
        }
    }
}