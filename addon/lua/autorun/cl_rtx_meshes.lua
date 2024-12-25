local function InitializeMeshSystem()
    if not EnableCustomRendering then
        error("RTX mesh system binary module not loaded!")
        return
    end
    
    -- Create ConVars
    local rtx_force_render = CreateClientConVar("rtx_force_render", "1", true, false)
    local rtx_chunk_size = CreateClientConVar("rtx_chunk_size", "512", true, false)
    
    -- Hook into rendering
    hook.Add("PreDrawOpaqueRenderables", "RTXMeshSystem", function()
        if rtx_force_render:GetBool() then
            render.SetBlend(1)
            RenderOpaqueChunks()  -- Binary module function
            return true
        end
    end)
    
    hook.Add("PreDrawTranslucentRenderables", "RTXMeshSystem", function()
        if rtx_force_render:GetBool() then
            RenderTranslucentChunks()  -- Binary module function
            return true
        end
    end)
    
    -- ConVar changes
    cvars.AddChangeCallback("rtx_force_render", function(_, _, new)
        EnableCustomRendering(tobool(new))
    end)
    
    cvars.AddChangeCallback("rtx_chunk_size", function()
        RebuildMeshes()
    end)
end

hook.Add("InitPostEntity", "RTXMeshInit", InitializeMeshSystem)