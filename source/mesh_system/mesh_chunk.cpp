// source/mesh_system/mesh_chunk.cpp
#include "mesh_chunk.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterialsystem.h"
#include "tier0/dbg.h"

// External engine interfaces
extern IMaterialSystem* g_materials;
extern IVEngineClient* g_engine;
extern IVModelInfo* g_modelinfo;
extern IClientEntityList* g_entitylist;

MeshChunk::MeshChunk(IMaterial* material)
    : m_material(material)
    , m_mesh(nullptr) {
    if (m_material) {
        m_material->IncrementReferenceCount();
    }
}

MeshChunk::~MeshChunk() {
    CleanupMesh();
    if (m_material) {
        m_material->DecrementReferenceCount();
        m_material = nullptr;
    }
}

bool MeshChunk::AddFace(const std::vector<Vertex>& vertices, const std::vector<unsigned short>& indices) {
    if (vertices.empty() || indices.empty()) {
        return false;
    }

    // Check if adding these vertices would exceed the limit
    if (m_vertices.size() + vertices.size() > 32768) { // Direct3D 9 vertex limit
        Warning("[Mesh Chunk] Attempted to exceed vertex limit\n");
        return false;
    }

    // Store current vertex count for index adjustment
    size_t baseVertex = m_vertices.size();

    // Add vertices
    m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());

    // Add and adjust indices
    for (unsigned short index : indices) {
        m_indices.push_back(baseVertex + index);
    }

    // Rebuild mesh if we already had one
    if (m_mesh) {
        CleanupMesh();
        return BuildMesh();
    }

    return true;
}

bool MeshChunk::BuildMesh() {
    if (!g_materials || !m_material || m_vertices.empty() || m_indices.empty()) {
        return false;
    }

    CleanupMesh(); // Ensure any existing mesh is cleaned up

    IMatRenderContext* pRenderContext = materials->GetRenderContext();
    if (!pRenderContext) {
        Warning("[Mesh Chunk] Failed to get render context\n");
        return false;
    }

    // Create the mesh
    m_mesh = pRenderContext->CreateStaticMesh(VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TEXCOORD_SIZE(2), m_material->GetName());
    if (!m_mesh) {
        Warning("[Mesh Chunk] Failed to create static mesh\n");
        return false;
    }

    CMeshBuilder meshBuilder;
    meshBuilder.Begin(m_mesh, MATERIAL_TRIANGLES, m_vertices.size(), m_indices.size());

    // Add vertices
    for (const Vertex& vert : m_vertices) {
        meshBuilder.Position3f(vert.pos.x, vert.pos.y, vert.pos.z);
        meshBuilder.Normal3f(vert.normal.x, vert.normal.y, vert.normal.z);
        meshBuilder.TexCoord2f(0, vert.uv.x, vert.uv.y);
        meshBuilder.Color4ub(vert.color[0], vert.color[1], vert.color[2], vert.color[3]);
        meshBuilder.AdvanceVertex();
    }

    // Add indices
    for (unsigned short index : m_indices) {
        meshBuilder.Index(index);
        meshBuilder.AdvanceIndex();
    }

    meshBuilder.End();
    return true;
}

void MeshChunk::Draw() {
    if (!m_mesh) {
        if (!BuildMesh()) {
            return;
        }
    }

    m_mesh->Draw();
}

bool MeshChunk::IsValid() const {
    return m_material && m_mesh && !m_vertices.empty() && !m_indices.empty();
}

void MeshChunk::CleanupMesh() {
    if (m_mesh) {
        m_mesh->DestroyVertexBuffers();
        delete m_mesh;
        m_mesh = nullptr;
    }
}

// Move constructor
MeshChunk::MeshChunk(MeshChunk&& other) noexcept
    : m_material(other.m_material)
    , m_vertices(std::move(other.m_vertices))
    , m_indices(std::move(other.m_indices))
    , m_mesh(other.m_mesh) {
    other.m_material = nullptr;
    other.m_mesh = nullptr;
}

// Move assignment operator
MeshChunk& MeshChunk::operator=(MeshChunk&& other) noexcept {
    if (this != &other) {
        CleanupMesh();
        if (m_material) {
            m_material->DecrementReferenceCount();
        }

        m_material = other.m_material;
        m_vertices = std::move(other.m_vertices);
        m_indices = std::move(other.m_indices);
        m_mesh = other.m_mesh;

        other.m_material = nullptr;
        other.m_mesh = nullptr;
    }
    return *this;
}