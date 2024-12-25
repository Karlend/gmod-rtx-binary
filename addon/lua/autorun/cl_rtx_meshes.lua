if not CLIENT then return end

print("[RTX Mesh] Initializing...")
print("[RTX Mesh] EnableCustomRendering function exists:", EnableCustomRendering ~= nil)
print("[RTX Mesh] RenderOpaqueChunks function exists:", RenderOpaqueChunks ~= nil)

local function InitializeMeshSystem()
    if not EnableCustomRendering then
        error("RTX mesh system binary module not loaded!")
        return
    end
    
    -- Create ConVars
    local rtx_force_render = CreateClientConVar("rtx_force_render", "1", true, false)
    
    -- Hook into rendering
    hook.Add("PreDrawOpaqueRenderables", "RTXMeshSystem", function()
        if rtx_force_render:GetBool() then
            RenderOpaqueChunks()  -- We need to expose this function too
            return true
        end
    end)
    
    -- Initial setup
    if rtx_force_render:GetBool() then
        EnableCustomRendering(true)
    end
end

hook.Add("InitPostEntity", "RTXMeshInit", InitializeMeshSystem)