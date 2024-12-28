if SERVER then return end

-- ConVars are created by the module
CreateClientConVar("rtx_ff_debug_hud", "1", true, false, "Show fixed function debug HUD")

local function CreateDebugHUD()
    if not GetConVar("rtx_ff_debug_hud"):GetBool() then return end
    
    -- Get stats from module
    local stats = FixedFunction.GetStats()
    if not stats then return end
    
    -- Draw debug info
    local text = string.format(
        "Fixed Function Pipeline:\nTotal Draws: %d\nFF Draws: %d",
        stats.total_draws or 0,
        stats.ff_draws or 0
    )
    
    draw.SimpleText(text, "Default", 10, 10, color_white)
end
hook.Add("HUDPaint", "FixedFunctionDebug", CreateDebugHUD)

-- Console commands
concommand.Add("ff_toggle", function()
    local current = GetConVar("rtx_ff_enable"):GetBool()
    FixedFunction.Enable(not current)
end)

-- Print info when loaded
hook.Add("InitPostEntity", "FixedFunctionInit", function()
    print("Fixed Function Pipeline v" .. FixedFunction.Version .. " loaded")
    print("Use ff_toggle to toggle fixed function pipeline")
end)