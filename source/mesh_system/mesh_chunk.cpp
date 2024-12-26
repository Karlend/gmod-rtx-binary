// source/mesh_system/mesh_chunk.cpp
#include "mesh_chunk.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/ishaderapi.h"
#include "tier0/dbg.h"
#include <algorithm>

// External engine interfaces - should be declared at the top
extern IMaterialSystem* g_materials;

MeshChunk::MeshChunk(IMaterial* material)
    : m_material(material)
    , m_mesh(nullptr) {
    if (m_material) {
        m_material->IncrementReferenceCount();
    }
}

bool MeshChunk::BuildMesh() {
    if (!g_materials || !m_material || m_vertices.empty() || m_indices.empty()) {
        return false;
    }

    CleanupMesh();

    IMatRenderContext* pRenderContext = g_materials->GetRenderContext();
    if (!pRenderContext) {
        Warning("[Mesh Chunk] Failed to get render context\n");
        return false;
    }

    // Create mesh with proper vertex format
    VertexFormat_t vertexFormat = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TEXCOORD_SIZE(1, 2) | VERTEX_COLOR;
    m_mesh = pRenderContext->CreateStaticMesh(vertexFormat, m_material->GetName());

    if (!m_mesh) {
        Warning("[Mesh Chunk] Failed to create static mesh\n");
        return false;
    }

    // Lock mesh for writing
    int maxVerts = m_vertices.size();
    int maxIndices = m_indices.size();
    
    VertexDesc_t vertDesc;
    void* vertData = m_mesh->LockMesh(maxVerts, maxIndices, &vertDesc);
    if (!vertData) {
        Warning("[Mesh Chunk] Failed to lock mesh\n");
        return false;
    }

    // Write vertex data
    for (size_t i = 0; i < m_vertices.size(); i++) {
        const Vertex& vert = m_vertices[i];
        
        // Position
        float* pos = (float*)((byte*)vertData + vertDesc.m_VertexSize_Position * i);
        pos[0] = vert.pos.x;
        pos[1] = vert.pos.y;
        pos[2] = vert.pos.z;

        // Normal
        float* normal = (float*)((byte*)vertData + vertDesc.m_VertexSize_Normal * i);
        normal[0] = vert.normal.x;
        normal[1] = vert.normal.y;
        normal[2] = vert.normal.z;

        // TexCoord
        float* texcoord = (float*)((byte*)vertData + vertDesc.m_VertexSize_TexCoord[0] * i);
        texcoord[0] = vert.uv.x;
        texcoord[1] = vert.uv.y;

        // Color
        unsigned char* color = (unsigned char*)((byte*)vertData + vertDesc.m_VertexSize_Color * i);
        color[0] = vert.color[0];
        color[1] = vert.color[1];
        color[2] = vert.color[2];
        color[3] = vert.color[3];
    }

    // Write indices
    unsigned short* pIndices = m_mesh->GetIndexBuffer();
    if (pIndices) {
        memcpy(pIndices, m_indices.data(), sizeof(unsigned short) * m_indices.size());
    }

    // Unlock and set mesh properties
    m_mesh->UnlockMesh(m_vertices.size(), m_indices.size());
    m_mesh->SetPrimitiveType(MATERIAL_TRIANGLES);

    return true;
}

void MeshChunk::Draw() const {
    if (!m_mesh) {
        // Need to cast away const to build mesh
        if (!const_cast<MeshChunk*>(this)->BuildMesh()) {
            return;
        }
    }

    if (m_material) {
        g_materials->GetRenderContext()->Bind(m_material);
    }
    
    m_mesh->Draw();
}

bool MeshChunk::IsValid() const {
    return m_material && m_mesh && !m_vertices.empty() && !m_indices.empty();
}

void MeshChunk::CleanupMesh() {
    if (m_mesh) {
        // IMesh will be automatically destroyed by the material system
        m_mesh = nullptr;
    }
}

// Move operations
MeshChunk::MeshChunk(MeshChunk&& other) noexcept
    : m_material(other.m_material)
    , m_vertices(std::move(other.m_vertices))
    , m_indices(std::move(other.m_indices))
    , m_mesh(other.m_mesh) {
    other.m_material = nullptr;
    other.m_mesh = nullptr;
}

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