if not CLIENT then return end

local function BuildRTXMenu(panel)
    panel:ClearControls()
    
    -- Enable/Disable control
    local enableCheck = panel:CheckBox("Enable Custom World Rendering", "rtx_force_render")
    enableCheck.OnChange = function(self, value)
        if value then
            EnableCustomRendering()
        else
            DisableCustomRendering()
        end
    end
    
    -- Chunk size control
    local sizeSlider = panel:NumSlider("Chunk Size", "rtx_chunk_size", 4, 8196, 0)
    sizeSlider.OnValueChanged = function(self, value)
        RTX.SetChunkSize(value)
    end
    
    -- Debug mode
    panel:CheckBox("Show Debug Info", "rtx_force_render_debug")
    
    -- Rebuild button
    local rebuildBtn = panel:Button("Rebuild Meshes")
    rebuildBtn.DoClick = function()
        RebuildMeshes()
    end
    
    -- Stats display if debug mode is on
    if GetConVar("rtx_force_render_debug"):GetBool() then
        local stats = GetRenderStats()
        if stats then
            panel:Help(string.format("Draw Calls: %d", stats.draws))
            panel:Help(string.format("Active Chunks: %d", stats.chunks))
            panel:Help(string.format("Total Vertices: %d", stats.vertices))
        end
    end
end

hook.Add("PopulateToolMenu", "RTXCustomWorldMenu", function()
    spawnmenu.AddToolMenuOption("Utilities", "User", "RTX_ForceRender", 
        "#RTX Custom World", "", "", BuildRTXMenu)
end)