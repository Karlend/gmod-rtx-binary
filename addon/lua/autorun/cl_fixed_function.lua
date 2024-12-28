if not CLIENT then return end

-- Simple state tracking
local ff_enabled = false

local function InitFF()
    if not _G.FixedFunction then
        ErrorNoHalt("[Fixed Function] Module not loaded, retrying in 1 second...\n")
        timer.Simple(1, InitFF)
        return
    end

    local FF = _G.FixedFunction

    -- Create toggle command
    concommand.Add("ff_toggle", function()
        ff_enabled = not ff_enabled
        print(string.format("[Fixed Function] %s", ff_enabled and "Enabled" or "Disabled"))
        FF.Enable(ff_enabled)
    end)

    print("[Fixed Function] Interface loaded - Version:", FF.Version)
end

-- Initialize when game is ready
hook.Add("InitPostEntity", "FixedFunctionInit", InitFF)