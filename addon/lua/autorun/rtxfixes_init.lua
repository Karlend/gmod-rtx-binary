if SERVER then return end

-- Load the appropriate binary module
require((BRANCH == "x86-64" or BRANCH == "chromium" ) and "RTXFixesBinary" or "RTXFixesBinary_32bit")

-- Simple initialization print
if RTX and RTX.Loaded then
    print("[RTX FVF] Module loaded successfully")
else

end