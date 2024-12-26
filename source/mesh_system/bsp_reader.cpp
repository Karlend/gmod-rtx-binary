#include "bsp_reader.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"
#include "bspflags.h"  // Add this for SURF_ flags
#include <tier0/dbg.h>
#include <algorithm>
#include "mathlib/mathlib.h"
#include "icliententity.h"

extern IMaterialSystem* g_materials;


IMaterial* BSPReader::GetCachedMaterial(const char* textureName) const {
    if (!textureName) return nullptr;

    auto it = m_materialCache.find(textureName);
    if (it != m_materialCache.end()) {
        return it->second;
    }

    // If not in cache, load and cache it
    IMaterial* material = g_materials->FindMaterial(textureName, TEXTURE_GROUP_WORLD);
    if (!material || material->IsErrorMaterial()) {
        return nullptr;
    }

    material->IncrementReferenceCount();
    const_cast<BSPReader*>(this)->m_materialCache[textureName] = material;
    return material;
}

// Add debug logging helper
void BSPReader::LogDebug(const char* format, ...) const {
    if (!m_config.debugMode) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Msg("[BSP Reader] %s", buffer);
}

// Helper function to get msurface_t from model data
msurface_t* GetModelSurface(model_t* model, int surfaceIndex) {
    if (!model || surfaceIndex < 0) return nullptr;

    // Get surface array from model
    msurface_t* surfaces = model->surface;
    int surfaceCount = model->numsurfaces;

    if (!surfaces || surfaceIndex >= surfaceCount) {
        return nullptr;
    }

    return &surfaces[surfaceIndex];
}

bool BSPFace::GetCenter(Vector& center) const {
    if (!m_surface || !m_surface->verts) return false;

    center = Vector(0, 0, 0);
    for (int i = 0; i < m_surface->numverts; i++) {
        center += m_surface->verts[i].position;
    }
    
    if (m_surface->numverts > 0) {
        center /= (float)m_surface->numverts;
        return true;
    }
    
    return false;
}

BSPFace::BSPFace(msurface_t* surface, byte* modelBasis) 
    : m_surface(surface), m_modelBasis(modelBasis) {
}

bool BSPFace::ShouldRender() const {
    if (!m_surface) return false;

    // Check if surface has valid material
    mtexinfo_t* texinfo = m_surface->texinfo;
    if (!texinfo || !texinfo->material) return false;

    // Skip nodraw surfaces
    if (texinfo->flags & SURF_NODRAW) return false;

    // Skip hint/skip surfaces
    if (texinfo->flags & (SURF_HINT | SURF_SKIP)) return false;

    return true;
}

bool BSPFace::IsSky() const {
    // For now, assume no sky faces
    return false;
}

bool BSPFace::GetVertexData(std::vector<Vertex>& vertices, 
                           std::vector<unsigned short>& indices) {
    // Handle displacement surfaces
    if (IsDisplacement()) {
        return GetDisplacementVertexData(vertices, indices);
    }

    // Regular surface handling
    if (!m_surface || !m_surface->verts) return false;

    // Get surface info
    msurface_t* surf = m_surface;
    mtexinfo_t* texinfo = surf->texinfo;
    if (!texinfo) return false;

    // Reserve space
    vertices.reserve(surf->numverts);
    indices.reserve(surf->numindexes);

    // Get vertices
    for (int i = 0; i < surf->numverts; i++) {
        Vertex vert;
        vert.pos = surf->verts[i].position;
        vert.normal = surf->verts[i].normal;

        // Calculate texture coordinates
        Vector pos = vert.pos;
        float s = DotProduct(pos, texinfo->textureVecs[0]) + texinfo->textureVecs[0][3];
        float t = DotProduct(pos, texinfo->textureVecs[1]) + texinfo->textureVecs[1][3];

        // Scale texture coordinates
        if (texinfo->material) {
            float width = texinfo->material->GetMappingWidth();
            float height = texinfo->material->GetMappingHeight();
            if (width > 0 && height > 0) {
                s /= width;
                t /= height;
            }
        }
        vert.uv = Vector2D(s, t);

        // Get lighting
        Vector lightColor = GetLightmapColor(i);
        vert.color[0] = (unsigned char)(lightColor.x * 255);
        vert.color[1] = (unsigned char)(lightColor.y * 255);
        vert.color[2] = (unsigned char)(lightColor.z * 255);
        vert.color[3] = 255;

        vertices.push_back(vert);
    }

    // Get indices
    for (int i = 0; i < surf->numindexes; i++) {
        indices.push_back(surf->indexes[i]);
    }

    return !vertices.empty() && !indices.empty();
}

bool BSPReader::LoadLeafs() {
    if (!m_header) return false;

    // Get leaf lump
    lump_t* leafLump = &m_header->lumps[LUMP_LEAFS];
    if (leafLump->filelen <= 0) return false;

    // Get visibility data
    lump_t* visLump = &m_header->lumps[LUMP_VISIBILITY];
    byte* visData = nullptr;
    if (visLump->filelen > 0) {
        visData = m_base + visLump->fileofs;
    }

    // Load all leafs
    dleaf_t* leafs = (dleaf_t*)(m_base + leafLump->fileofs);
    int leafCount = leafLump->filelen / sizeof(dleaf_t);

    m_leafs.reserve(leafCount);
    for (int i = 0; i < leafCount; i++) {
        m_leafs.emplace_back(&leafs[i], m_base);
    }

    return true;
}

bool BSPFace::IsTranslucent() const {
    if (!m_surface || !m_surface->texinfo) return false;
    
    IMaterial* material = m_surface->texinfo->material;
    return material && material->IsTranslucent();
}

bool BSPFace::IsSky() const {
    if (!m_surface || !m_surface->texinfo) return false;
    return (m_surface->texinfo->flags & SURF_SKY) != 0;
}

bool BSPFace::IsDisplacement() const {
    return m_surface && m_surface->dispinfo != nullptr;
}

bool BSPFace::GetDisplacementVertexData(std::vector<Vertex>& vertices, 
                                      std::vector<unsigned short>& indices) {
    if (!IsDisplacement() || !m_surface->dispinfo) return false;

    CDispInfo* disp = m_surface->dispinfo;
    int power = disp->GetPower();
    int size = (1 << power) + 1;
    int vertCount = size * size;

    // Reserve space
    vertices.reserve(vertCount);

    // Get base surface corners
    Vector baseVerts[4];
    for (int i = 0; i < 4; i++) {
        baseVerts[i] = m_surface->verts[i].position;
    }

    // Generate displacement vertices
    for (int y = 0; y < size; y++) {
        float fy = (float)y / (size - 1);
        for (int x = 0; x < size; x++) {
            float fx = (float)x / (size - 1);
            
            // Calculate base position
            Vector pos;
            pos = baseVerts[0] * ((1.0f - fx) * (1.0f - fy)) +
                  baseVerts[1] * (fx * (1.0f - fy)) +
                  baseVerts[2] * (fx * fy) +
                  baseVerts[3] * ((1.0f - fx) * fy);

            // Get displacement info
            int index = y * size + x;
            Vector normal;
            float alpha;
            disp->GetVert(index, pos, normal, alpha);

            // Calculate texture coordinates
            Vector2D texCoord;
            disp->GetTexCoord(index, texCoord);

            // Create vertex
            Vertex vert;
            vert.pos = pos;
            vert.normal = normal;
            vert.uv = texCoord;
            
            // Get vertex lighting
            Vector lightColor = GetDisplacementLightmapColor(index);
            vert.color[0] = (unsigned char)(lightColor.x * 255);
            vert.color[1] = (unsigned char)(lightColor.y * 255);
            vert.color[2] = (unsigned char)(lightColor.z * 255);
            vert.color[3] = (unsigned char)(alpha * 255);

            vertices.push_back(vert);
        }
    }

    // Generate indices for displacement
    indices.reserve((size - 1) * (size - 1) * 6);
    for (int y = 0; y < size - 1; y++) {
        for (int x = 0; x < size - 1; x++) {
            int baseIndex = y * size + x;
            
            // First triangle
            indices.push_back(baseIndex);
            indices.push_back(baseIndex + 1);
            indices.push_back(baseIndex + size);
            
            // Second triangle
            indices.push_back(baseIndex + 1);
            indices.push_back(baseIndex + size + 1);
            indices.push_back(baseIndex + size);
        }
    }

    return true;
}

Vector BSPFace::GetDisplacementLightmapColor(int index) const {
    if (!m_surface || !m_surface->dispinfo) return Vector(1, 1, 1);

    CDispInfo* disp = m_surface->dispinfo;
    Vector color;
    if (disp->GetLightmapSample(index, color)) {
        return color;
    }
    return Vector(1, 1, 1);
}

IMaterial* BSPFace::GetMaterial() const {
    if (!m_surface || !m_surface->texinfo) {
        return g_materials->FindMaterial("debug/debugempty", TEXTURE_GROUP_OTHER);
    }
    return m_surface->texinfo->material;
}

int BSPFace::NumIndices() const {
    // For our test quad
    return 6;  // 2 triangles = 6 indices
}

unsigned short BSPFace::GetIndex(int index) const {
    // Indices for our test quad
    static const unsigned short indices[] = { 0, 1, 2, 0, 2, 3 };
    if (index >= 0 && index < 6) {
        return indices[index];
    }
    return 0;
}

Vector BSPFace::GetLightmapColor(int vertex) const {
    if (!m_surface || vertex < 0 || vertex >= m_surface->numverts) {
        return Vector(1, 1, 1);  // Return white if invalid
    }

    // Get lightmap data
    if (m_surface->lightmaptexturenum >= 0 && m_surface->lightmaptexturenum < MAX_LIGHTMAPS) {
        ColorRGBExp32* samples = m_surface->samples;
        if (samples) {
            // Convert ColorRGBExp32 to Vector
            ColorRGBExp32& sample = samples[vertex];
            float r = sample.r * pow(2.0f, sample.exponent) / 255.0f;
            float g = sample.g * pow(2.0f, sample.exponent) / 255.0f;
            float b = sample.b * pow(2.0f, sample.exponent) / 255.0f;
            
            // Clamp values
            r = Clamp(r, 0.0f, 1.0f);
            g = Clamp(g, 0.0f, 1.0f);
            b = Clamp(b, 0.0f, 1.0f);
            
            return Vector(r, g, b);
        }
    }

    return Vector(1, 1, 1);  // Default to white if no lightmap
}

// BSPLeaf implementation with stub functions for now
BSPLeaf::BSPLeaf(void* leaf, byte* modelBasis) 
    : m_leaf(leaf), m_modelBasis(modelBasis) {
    if (!leaf || !modelBasis) return;

    dleaf_t* bspLeaf = static_cast<dleaf_t*>(leaf);
    
    // Get world model
    model_t* worldModel = g_modelinfo->GetModel(0);
    if (!worldModel) return;

    // Load faces from leaf
    for (int i = 0; i < bspLeaf->numleaffaces; i++) {
        unsigned short faceIndex = ((unsigned short*)((byte*)modelBasis + 
            worldModel->leaffaces))[bspLeaf->firstleafface + i];

        if (faceIndex < worldModel->numsurfaces) {
            msurface_t* surface = &worldModel->surfaces[faceIndex];
            if (surface) {
                m_faces.emplace_back(surface, modelBasis);
            }
        }
    }
}

bool BSPLeaf::IsOutsideMap() const {
    if (!m_leaf) return true;
    dleaf_t* bspLeaf = static_cast<dleaf_t*>(m_leaf);
    return (bspLeaf->contents & CONTENTS_SOLID) != 0;
}

int BSPLeaf::NumFaces() const {
    return m_faces.size();
}

BSPFace* BSPLeaf::GetFace(int index) {
    if (index < 0 || index >= m_faces.size()) return nullptr;
    return &m_faces[index];
}

// BSPReader implementation
BSPReader::BSPReader() 
    : m_worldModel(nullptr), m_header(nullptr), m_base(nullptr) {
}

BSPReader::~BSPReader() {
    Unload();
}

bool BSPReader::LoadTextures() {
    if (!m_header) return false;

    // Get texture data lumps
    lump_t* texDataLump = &m_header->lumps[LUMP_TEXDATA];
    lump_t* texInfoLump = &m_header->lumps[LUMP_TEXINFO];
    
    if (texDataLump->filelen <= 0 || texInfoLump->filelen <= 0) return false;

    dtexdata_t* texdata = (dtexdata_t*)(m_base + texDataLump->fileofs);
    texinfo_t* texinfo = (texinfo_t*)(m_base + texInfoLump->fileofs);

    int texDataCount = texDataLump->filelen / sizeof(dtexdata_t);
    int texInfoCount = texInfoLump->filelen / sizeof(texinfo_t);

    // Clear existing caches
    m_materialCache.clear();
    m_materials.clear();

    LogDebug("Loading %d texinfos...\n", texInfoCount);

    // Load and cache materials
    m_materials.reserve(texDataCount);
    for (int i = 0; i < texInfoCount; i++) {
        const char* textureName = GetTextureName(texinfo[i].texdata);
        if (!textureName) continue;

        // Check if we already loaded this material
        auto it = m_materialCache.find(textureName);
        if (it != m_materialCache.end()) {
            continue;
        }

        IMaterial* material = g_materials->FindMaterial(textureName, TEXTURE_GROUP_WORLD);
        if (!material || material->IsErrorMaterial()) {
            LogDebug("Failed to load material: %s\n", textureName);
            continue;
        }

        material->IncrementReferenceCount();
        m_materialCache[textureName] = material;
        m_materials.push_back(material);

        if (m_config.debugMode) {
            LogDebug("Loaded material: %s\n", textureName);
        }
    }

    LogDebug("Loaded %zu unique materials\n", m_materialCache.size());
    return true;
}

bool BSPReader::Load(model_t* worldModel) {
    Unload();
    
    if (!worldModel) {
        Warning("[BSP Reader] Invalid world model\n");
        return false;
    }

    m_worldModel = worldModel;
    m_base = (byte*)worldModel;
    m_header = (dheader_t*)m_base;

    if (!ValidateHeader()) {
        Warning("[BSP Reader] Invalid BSP header\n");
        return false;
    }

    if (!LoadLeafs() || !LoadTextures()) {
        Warning("[BSP Reader] Failed to load BSP data\n");
        return false;
    }

    Msg("[BSP Reader] Successfully loaded BSP with %d leafs and %d textures\n",
        m_leafs.size(), m_materials.size());
    return true;
}

void BSPReader::Unload() {
    // Release material references
    for (auto& pair : m_materialCache) {
        if (pair.second) {
            pair.second->DecrementReferenceCount();
        }
    }
    
    m_materialCache.clear();
    m_materials.clear();
    m_leafs.clear();
    m_worldModel = nullptr;
    m_header = nullptr;
    m_base = nullptr;
}

bool BSPReader::ValidateHeader() const {
    if (!m_header) return false;
    
    // Check BSP version
    if (m_header->version < MINBSPVERSION || m_header->version > BSPVERSION) {
        Warning("[BSP Reader] Unsupported BSP version %d\n", m_header->version);
        return false;
    }

    return true;
}




int BSPReader::NumLeafs() const {
    return m_leafs.size();
}

BSPLeaf* BSPReader::GetLeaf(int index) {
    if (index < 0 || index >= m_leafs.size()) return nullptr;
    return &m_leafs[index];
}

const char* BSPReader::GetTextureName(int index) const {
    if (!m_header || index < 0) return nullptr;
    
    lump_t* stringLump = &m_header->lumps[LUMP_TEXDATA_STRING_DATA];
    if (stringLump->filelen <= 0) return nullptr;

    lump_t* stringTableLump = &m_header->lumps[LUMP_TEXDATA_STRING_TABLE];
    if (stringTableLump->filelen <= 0) return nullptr;

    int* stringTable = (int*)(m_base + stringTableLump->fileofs);
    char* stringData = (char*)(m_base + stringLump->fileofs);

    if (index >= stringTableLump->filelen / sizeof(int)) return nullptr;
    
    int stringOffset = stringTable[index];
    if (stringOffset >= stringLump->filelen) return nullptr;

    return &stringData[stringOffset];
}

IMaterial* BSPReader::GetTextureMaterial(int index) const {
    if (index < 0 || index >= m_materials.size()) return nullptr;
    return m_materials[index];
}