#pragma once
#include "mathlib/vector.h"
#include "mathlib/vector2d.h"
#include "bspfile.h"
#include "cmodel.h"
#include "materialsystem/imaterial.h"
#include "utlvector.h"
#include <vector>
#include <string>
#include <unordered_map>
#include "mesh_chunk.h"
#include "dispcoll.h" // For CDispInfo
#include "studio.h" // Contains surface definitions
#include "bsp_structs.h"

// Forward declarations for Source Engine types we need
struct model_t;
struct msurface_t;
struct mvertex_t;
struct mtexinfo_t;
struct dplane_t;
struct ColorRGBExp32;

class BSPFace {
public:
    BSPFace(msurface_t* surface, byte* modelBasis);
    
    // Public interface
    bool ShouldRender() const;
    bool IsSky() const;
    IMaterial* GetMaterial() const;
    bool GetVertexData(std::vector<Vertex>& vertices, std::vector<unsigned short>& indices);
    int NumIndices() const;
    unsigned short GetIndex(int index) const;

    // Make these public as they're used by other classes
    bool GetCenter(Vector& center) const;
    bool IsTranslucent() const;
    bool IsDisplacement() const;
    Vector GetLightmapColor(int vertex) const;
    Vector GetDisplacementLightmapColor(int index) const;

private:
    msurface_t* m_surface;
    byte* m_modelBasis;
};

class BSPLeaf {
public:
    BSPLeaf(void* leaf, byte* modelBasis);
    
    bool IsOutsideMap() const;
    int NumFaces() const;
    BSPFace* GetFace(int index);

private:
    void* m_leaf;
    byte* m_modelBasis;
    std::vector<BSPFace> m_faces;
};

class BSPReader {
public:
    BSPReader();
    ~BSPReader();

    bool Load(model_t* worldModel);
    void Unload();
    
    int NumLeafs() const;
    BSPLeaf* GetLeaf(int index);

    // Helper functions
    const char* GetTextureName(int index) const;
    IMaterial* GetTextureMaterial(int index) const;
    IMaterial* GetCachedMaterial(const char* textureName) const;  // Moved from BSPFace
    
private:
    bool LoadLeafs();
    bool LoadTextures();
    bool ValidateHeader() const;
    void LogDebug(const char* format, ...) const;

    model_t* m_worldModel;
    dheader_t* m_header;
    byte* m_base;

    struct Config {
        bool debugMode = false;
    } m_config;
    
    std::unordered_map<std::string, IMaterial*> m_materialCache;
    std::vector<IMaterial*> m_materials;
    std::vector<BSPLeaf> m_leafs;
};