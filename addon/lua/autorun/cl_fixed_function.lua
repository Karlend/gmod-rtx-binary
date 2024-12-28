if not CLIENT then return end

local function InitializeFixedFunction()
    local FF = _G.FixedFunction
    if not FF then
        Error("[Fixed Function] Module not loaded properly! Retrying in 1 second...\n")
        timer.Simple(1, InitializeFixedFunction)
        return
    end

    -- Create ConVars
    CreateClientConVar("rtx_ff_debug_hud", "1", true, false, "Show fixed function debug HUD")

    -- Add commands
    concommand.Add("ff_toggle", function()
        if not FF.Enable then 
            Error("[Fixed Function] Module interface not available!\n")
            return
        end
        
        local current = GetConVar("rtx_ff_enable"):GetBool()
        Msg(string.format("[Fixed Function] Toggling state from %s to %s\n", 
            current and "on" or "off",
            (not current) and "on" or "off"))
        
        FF.Enable(not current)
    end)

    Msg(string.format("[Fixed Function] Module v%s loaded successfully\n", FF.Version))
    Msg("[Fixed Function] Use ff_toggle to toggle fixed function pipeline\n")
end

-- Start initialization
hook.Add("Initialize", "FixedFunctionInit", InitializeFixedFunction)