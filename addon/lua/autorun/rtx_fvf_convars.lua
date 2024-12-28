if SERVER then return end

local RTX_API = nil
local COMMANDS_READY = false

-- Create ConVars
CreateClientConVar("rtx_world_fvf", "1", true, false, "Enable FVF for world geometry rendering")
CreateClientConVar("rtx_model_fvf", "1", true, false, "Enable FVF for model rendering")

-- Helper function to check RTX availability
local function IsRTXReady()
    if not COMMANDS_READY then
        print("[RTX FVF] Waiting for module initialization...")
        return false
    end
    return true
end

-- Helper function for feedback
local function PrintRTXStatus(setting, enabled)
    local status = enabled and "enabled" or "disabled"
    print(string.format("[RTX FVF] %s is now %s", setting, status))
end

-- Command to toggle world FVF
concommand.Add("rtx_toggle_world_fvf", function(ply, cmd, args)
    if not IsRTXReady() then return end
    
    local currentValue = GetConVar("rtx_world_fvf"):GetBool()
    local newValue = not currentValue
    RunConsoleCommand("rtx_world_fvf", newValue and "1" or "0")
    if RTX_API and RTX_API.SetWorldFVF then
        RTX_API.SetWorldFVF(newValue)
    end
    PrintRTXStatus("World FVF", newValue)
end)

-- Command to toggle model FVF
concommand.Add("rtx_toggle_model_fvf", function(ply, cmd, args)
    if not IsRTXReady() then return end
    
    local currentValue = GetConVar("rtx_model_fvf"):GetBool()
    local newValue = not currentValue
    RunConsoleCommand("rtx_model_fvf", newValue and "1" or "0")
    if RTX_API and RTX_API.SetModelFVF then
        RTX_API.SetModelFVF(newValue)
    end
    PrintRTXStatus("Model FVF", newValue)
end)

-- Command to show current status
concommand.Add("rtx_fvf_status", function(ply, cmd, args)
    if not IsRTXReady() then return end

    local worldEnabled = GetConVar("rtx_world_fvf"):GetBool()
    local modelEnabled = GetConVar("rtx_model_fvf"):GetBool()
    
    print("=== RTX FVF Status ===")
    print("World FVF:", worldEnabled and "Enabled" or "Disabled")
    print("Model FVF:", modelEnabled and "Enabled" or "Disabled")
    print("Module Loaded:", RTX_API and "Yes" or "No")
end)

-- Helper function to set both
concommand.Add("rtx_set_fvf", function(ply, cmd, args)
    if not IsRTXReady() then return end
    
    if not args[1] then
        print("[RTX FVF] Usage: rtx_set_fvf <0/1>")
        return
    end

    local enable = args[1] == "1" or args[1] == "true"
    RunConsoleCommand("rtx_world_fvf", enable and "1" or "0")
    RunConsoleCommand("rtx_model_fvf", enable and "1" or "0")
    
    if RTX_API then
        if RTX_API.SetWorldFVF then RTX_API.SetWorldFVF(enable) end
        if RTX_API.SetModelFVF then RTX_API.SetModelFVF(enable) end
    end
    
    print(string.format("[RTX FVF] All FVF settings are now %s", enable and "enabled" or "disabled"))
end)

-- Wait for module initialization
hook.Add("InitPostEntity", "RTXFVFInit", function()
    timer.Simple(1, function()
        RTX_API = _G.RTX
        if RTX_API and RTX_API.Loaded then
            COMMANDS_READY = true
            print("[RTX FVF] Module initialized successfully")
            print("=== RTX FVF Commands ===")
            print("rtx_toggle_world_fvf - Toggle world FVF")
            print("rtx_toggle_model_fvf - Toggle model FVF")
            print("rtx_fvf_status      - Show current FVF status")
            print("rtx_set_fvf <0/1>   - Set all FVF settings")
        else
            ErrorNoHalt("[RTX FVF] Failed to initialize module\n")
        end
    end)
end)

-- Additional initialization check
hook.Add("Think", "RTXFVFInitCheck", function()
    if not COMMANDS_READY and not RTX_API then
        RTX_API = _G.RTX
        if RTX_API and RTX_API.Loaded then
            COMMANDS_READY = true
            print("[RTX FVF] Module initialized through Think hook")
        end
    else
        hook.Remove("Think", "RTXFVFInitCheck")
    end
end)