// source/mesh_system/mesh_chunk.h
#pragma once
#include "mathlib/vector.h"
#include "materialsystem/imaterial.h"
#include <vector>

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
    void Draw();
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