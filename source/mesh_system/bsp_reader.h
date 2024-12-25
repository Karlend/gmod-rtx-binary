#pragma once
#include "mathlib/vector.h"
#include "mathlib/vector2d.h"
#include "bspfile.h"
#include "cmodel.h"
#include "materialsystem/imaterial.h"
#include "utlvector.h"

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
    
    bool ShouldRender() const;
    bool IsSky() const;
    IMaterial* GetMaterial() const;
    
    bool GetVertexData(std::vector<Vector>& vertices, 
                      std::vector<Vector>& normals,
                      std::vector<Vector2D>& texcoords);
    
    int NumIndices() const;
    unsigned short GetIndex(int index) const;
    Vector GetLightmapColor(int vertex) const;

private:
    msurface_t* m_surface;
    byte* m_modelBasis;
};

class BSPLeaf {
public:
    BSPLeaf(void* leaf, byte* modelBasis);  // Changed from dleaf_t* to void*
    
    bool IsOutsideMap() const;
    int NumFaces() const;
    BSPFace* GetFace(int index);

private:
    void* m_leaf;  // Changed from dleaf_t*
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
    
private:
    bool LoadLeafs();
    bool LoadTextures();
    bool ValidateHeader() const;

    std::unordered_map<std::string, IMaterial*> m_materialCache;
    model_t* m_worldModel;
    dheader_t* m_header;
    byte* m_base;

    struct Config {
    bool debugMode = false;
    } m_config;
    
    std::vector<BSPLeaf> m_leafs;
    std::vector<IMaterial*> m_materials;
};