-- addon/lua/autorun/client/cl_rtx_meshes.lua
if not CLIENT then return end

local function InitializeMeshSystem()
    if not EnableCustomRendering then
        error("RTX mesh system binary module not loaded!")
        return
    end
    
    print("[RTX Mesh] Initializing mesh system...")
    
    -- Create ConVars
    local rtx_force_render = CreateClientConVar("rtx_force_render", "1", true, false)
    
    -- Hook into rendering - change to RenderMapGeometry
    hook.Add("PreDrawOpaqueRenderables", "RTXMeshSystem", function()
        if rtx_force_render:GetBool() then
            RenderMapGeometry()  -- New function that handles both passes
            return true
        end
    end)
    
    -- ConVar change callback
    cvars.AddChangeCallback("rtx_force_render", function(name, old, new)
        print("[RTX Mesh] rtx_force_render changed from", old, "to", new)
        EnableCustomRendering(tobool(new))
    end)

    -- Initial setup
    if rtx_force_render:GetBool() then
        print("[RTX Mesh] Enabling custom rendering...")
        EnableCustomRendering(true)
    end
end

hook.Add("InitPostEntity", "RTXMeshInit", InitializeMeshSystem)