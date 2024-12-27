ENT.Type = "anim"
ENT.Base = "base_gmodentity"
ENT.PrintName = "RTX Light"
ENT.Spawnable = true
ENT.AdminSpawnable = true
ENT.Category = "RTX"

function ENT:SetupDataTables()
    self:NetworkVar("Float", 0, "LightBrightness")
    self:NetworkVar("Float", 1, "LightSize")
    self:NetworkVar("Int", 0, "LightR")
    self:NetworkVar("Int", 1, "LightG")
    self:NetworkVar("Int", 2, "LightB")

    if SERVER then
        self:NetworkVarNotify("LightBrightness", self.OnVarChanged)
        self:NetworkVarNotify("LightSize", self.OnVarChanged)
        self:NetworkVarNotify("LightR", self.OnVarChanged)
        self:NetworkVarNotify("LightG", self.OnVarChanged)
        self:NetworkVarNotify("LightB", self.OnVarChanged)
    end
end

function ENT:OnVarChanged(name, old, new)
    -- Handle property changes
end

function ENT:OnVarChanged(name, old, new)
    if CLIENT then
        -- Queue an update when networked vars change
        if self.rtxLightHandle and self.rtxLightHandle > 0 then
            self:UpdateLight()
        end
    end
end