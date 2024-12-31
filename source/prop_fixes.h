#pragma once
#include <Windows.h>
#include "engine/ivmodelrender.h"
#include "model_types.h"
#include "e_utils.h"
#include "materialsystem/imaterialsystem.h"
#include "ivrenderview.h"

// Define render flags directly here
#define RTX_RENDER_FLAGS_FORCE_NO_VIS (1 << 0)
#define RTX_RENDER_FLAGS_DISABLE_RENDERING_CACHE (1 << 1)
#define RENDER_FLAGS_FORCE_NO_VIS RTX_RENDER_FLAGS_FORCE_NO_VIS
#define RENDER_FLAGS_DISABLE_RENDERING_CACHE RTX_RENDER_FLAGS_DISABLE_RENDERING_CACHE

// Extended version of WorldListInfo_t with our additional field
struct ExtendedWorldListInfo_t : public WorldListInfo_t
{
    int m_nRenderFlags;  // Additional field for our use
};

struct WorldListLeafData_t {
    int leafIndex;
};

class ModelRenderHooks {
public:
    static ModelRenderHooks& Instance() {
        static ModelRenderHooks instance;
        return instance;
    }

    void Initialize();
    void Shutdown();
    void SetNoVisFlags(bool enable);
    bool HasNoVisFlags() const;

private:
    // Private helper method for flag modification
    void ModifyRenderFlags(bool enable);

    // Hook objects
    Detouring::Hook m_DrawModelExecute_hook;
    Detouring::Hook m_CullBox_hook;
    Detouring::Hook m_ShouldDraw_hook;
    Detouring::Hook m_BuildWorldLists_hook;
    Detouring::Hook m_StudioSkinLighting_hook;

    // Interface pointers
    IVRenderView* m_pRenderView;
    IVModelRender* m_pModelRender;

    // State tracking
    bool m_noVisEnabled;
};