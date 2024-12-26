if CLIENT then
    -- Debug: Print engine info
    print("[RTX Debug] Engine table type:", type(engine))
    print("[RTX Debug] EngineClient type:", type(engineapi))
    
    local success, err = pcall(function()
        require((BRANCH == "x86-64" or BRANCH == "chromium" ) and "RTXFixesBinary" or "RTXFixesBinary_32bit")
    end)

    hook.Add("Think", "MaterialConverter_Cleanup", function()
        if (#m_materialCache > 1000) then  // Arbitrary threshold
            CleanupCache()
        end
    end)

    if not success then
        ErrorNoHalt("[RTX Material Fixes] Failed to load module: ", err, "\n")
        return
    end
end