if SERVER then return end

local function RunRenderTest()
    if not RTX or not RTX.StartLogging then
        error("RTX module not loaded correctly!")
        return
    end
    
    print("RTX Module status:")
    print("  Loaded:", RTX.Loaded)
    print("  Version:", RTX.Version)
    print("  RTX Enabled:", RTX.RTXEnabled)
    
    print("\nStarting render test sequence...")
    RTX.SetLoggingInterval(0.1)
    RTX.StartLogging()
    
    -- Wait a few seconds to capture normal world rendering
    timer.Simple(2, function()
        print("Captured world rendering...")
        
        -- Move the player to capture different views
        local angles = LocalPlayer():EyeAngles()
        LocalPlayer():SetEyeAngles(angles + Angle(45, 90, 0))
        
        -- Wait a bit more
        timer.Simple(2, function()
            print("Captured different view angles...")
            
            -- Spawn some props to capture model rendering
            RunConsoleCommand("gm_spawn", "models/props_c17/oildrum001.mdl")
            
            timer.Simple(2, function()
                print("Captured model rendering...")
                
                -- Test particles
                local emitter = ParticleEmitter(LocalPlayer():GetPos())
                if emitter then
                    for i=1, 10 do
                        local particle = emitter:Add("sprites/glow04_noz", LocalPlayer():GetPos())
                        if particle then
                            particle:SetVelocity(VectorRand() * 100)
                            particle:SetDieTime(2)
                            particle:SetStartAlpha(255)
                            particle:SetEndAlpha(0)
                            particle:SetStartSize(10)
                            particle:SetEndSize(0)
                            particle:SetRoll(math.Rand(0, 360))
                            particle:SetRollDelta(math.Rand(-2, 2))
                            particle:SetColor(255, 255, 255)
                        end
                    end
                    emitter:Finish()
                end
                
                -- Finally stop logging
                timer.Simple(2, function()
                    print("Captured particle effects...")
                    RTX.StopLogging()
                    print("Test sequence complete!")
                end)
            end)
        end)
    end)
end

concommand.Add("rtx_test_logging", function()
    local success, err = pcall(RunRenderTest)
    if not success then
        error("Failed to run render test: " .. tostring(err))
    end
end)

-- Print module status on load
if RTX and RTX.Loaded then
    print(string.format("RTX Module loaded - Version: %s (RTX %s)", 
        RTX.Version or "unknown",
        RTX.RTXEnabled and "Enabled" or "Disabled"))
else
end