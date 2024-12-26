// source/mesh_system/mesh_chunk.h
#pragma once
#include "mathlib/vector.h"
#include "materialsystem/imaterial.h"
#include <vector>
#include "materialsystem/ishaderapi.h"

#define SHADER_BLEND_SRC_ALPHA 4
#define SHADER_BLEND_ONE_MINUS_SRC_ALPHA 5
#define VERTEX_POSITION         0x0001
#define VERTEX_NORMAL          0x0002
#define VERTEX_COLOR           0x0004
#define VERTEX_TEXCOORD_SIZE(n,size)  (((size) & 0xFF) << (((n) & 0x3) * 8 + 8))

struct VertexDesc_t {
    int m_VertexSize_Position;
    int m_VertexSize_Normal;
    int m_VertexSize_Color;
    int m_VertexSize_TexCoord[8];
};

#define MATERIAL_TRIANGLES     2

struct Vertex {
    Vector pos;
    Vector normal;
    Vector2D uv;
    unsigned char color[4];
};

class MeshChunk {
public:
    Vector GetCenter() const {
        if (m_vertices.empty()) return Vector(0, 0, 0);
        
        Vector center(0, 0, 0);
        for (const auto& vert : m_vertices) {
            center += vert.pos;
        }
        return center / static_cast<float>(m_vertices.size());
    }

    MeshChunk(IMaterial* material);
    ~MeshChunk();

    // Move operations
    MeshChunk(MeshChunk&& other) noexcept;
    MeshChunk& operator=(MeshChunk&& other) noexcept;

    // Disable copy operations
    MeshChunk(const MeshChunk&) = delete;
    MeshChunk& operator=(const MeshChunk&) = delete;

    bool AddFace(const std::vector<Vertex>& vertices, const std::vector<unsigned short>& indices);
    void Draw() const;
    bool IsValid() const;
    
    IMaterial* GetMaterial() const { return m_material; }
    size_t GetVertexCount() const { return m_vertices.size(); }
    

private:
    IMaterial* m_material;
    std::vector<Vertex> m_vertices;
    std::vector<unsigned short> m_indices;
    IMesh* m_mesh;
    
    bool BuildMesh();
    void CleanupMesh();
};