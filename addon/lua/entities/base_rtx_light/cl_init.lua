include("shared.lua")

ENT.LastUpdateTime = 0
ENT.UpdateCooldown = 0.05 -- 50ms minimum between updates

function ENT:Initialize()
    self:SetNoDraw(true)
    self:DrawShadow(false)
    
    -- Delay light creation to ensure networked values are received
    timer.Simple(0.1, function()
        if IsValid(self) then
            self:CreateRTXLight()
        end
    end)
end

function ENT:CreateRTXLight()
    -- Clean up existing light if any
    if self.rtxLightHandle then
        pcall(function() 
            DestroyRTXLight(self.rtxLightHandle)
        end)
        self.rtxLightHandle = nil
    end

    local pos = self:GetPos()
    local size = self:GetLightSize()
    local brightness = self:GetLightBrightness()
    local r = self:GetLightR()
    local g = self:GetLightG()
    local b = self:GetLightB()

    -- Only create if we have valid values
    if not (size > 0 and brightness > 0) then return end

    local handle = CreateRTXLight(
        pos.x, 
        pos.y, 
        pos.z,
        size,
        brightness,
        r / 255, -- Convert to 0-1 range for binary module
        g / 255,
        b / 255
    )

    if handle and handle > 0 then
        self.rtxLightHandle = handle
        self.lastUpdatePos = pos
        self.LastUpdateTime = CurTime()
    end
end

function ENT:UpdateLight()
    if not self.rtxLightHandle or self.rtxLightHandle <= 0 then return false end
    
    local pos = self:GetPos()
    local size = self:GetLightSize()
    local brightness = self:GetLightBrightness()
    local r = self:GetLightR()
    local g = self:GetLightG()
    local b = self:GetLightB()

    -- Skip invalid values
    if not (size > 0 and brightness > 0) then return false end

    -- Queue update instead of immediate creation
    QueueRTXLightUpdate(
        self.rtxLightHandle,
        pos.x, 
        pos.y, 
        pos.z,
        size,
        brightness,
        r / 255,
        g / 255,
        b / 255,
        false -- not forced
    )

    self.lastUpdatePos = pos
    self.LastUpdateTime = CurTime()
    return true
end

function ENT:Think()
    -- Skip if too soon
    if CurTime() - self.LastUpdateTime < self.UpdateCooldown then return end
    
    -- Create light if we don't have one
    if not self.rtxLightHandle or self.rtxLightHandle <= 0 then
        self:CreateRTXLight()
        return
    end

    -- Check if we need to update
    local pos = self:GetPos()
    if not self.lastUpdatePos or pos:DistToSqr(self.lastUpdatePos) > 1 then
        self:UpdateLight()
    end
end

function ENT:UpdateLight()
    if not self.rtxLightHandle or self.rtxLightHandle <= 0 then return false end
    
    local pos = self:GetPos()
    local size = self:GetLightSize()
    local brightness = self:GetLightBrightness()
    local r = self:GetLightR()
    local g = self:GetLightG()
    local b = self:GetLightB()

    -- Skip invalid values
    if not (size > 0 and brightness > 0) then return false end

    -- Queue update directly using binary module function
    QueueRTXLightUpdate(
        self.rtxLightHandle,
        pos.x, 
        pos.y, 
        pos.z,
        size,
        brightness,
        r / 255,
        g / 255,
        b / 255,
        false -- not forced
    )

    self.lastUpdatePos = pos
    self.LastUpdateTime = CurTime()
    return true
end

-- Update OnPropertyChanged similarly
function ENT:OnPropertyChanged()
    if not self.rtxLightHandle or self.rtxLightHandle <= 0 then return end
    
    local pos = self:GetPos()
    QueueRTXLightUpdate(
        self.rtxLightHandle,
        pos.x, 
        pos.y, 
        pos.z,
        self:GetLightSize(),
        self:GetLightBrightness(),
        self:GetLightR() / 255,
        self:GetLightG() / 255,
        self:GetLightB() / 255,
        true -- force update
    )
end

function ENT:OnRemove()
    if self.rtxLightHandle and self.rtxLightHandle > 0 then
        pcall(function()
            DestroyRTXLight(self.rtxLightHandle)
        end)
        self.rtxLightHandle = nil
    end
end

-- Network cleanup handler
net.Receive("RTXLight_Cleanup", function()
    local ent = net.ReadEntity()
    if IsValid(ent) then
        ent:OnRemove()
    end
end)

-- Property menu implementation
function ENT:OnPropertyChanged()
    if not self.rtxLightHandle or self.rtxLightHandle <= 0 then return end
    
    local pos = self:GetPos()
    QueueRTXLightUpdate(
        self.rtxLightHandle,
        pos.x, 
        pos.y, 
        pos.z,
        self:GetLightSize(),
        self:GetLightBrightness(),
        self:GetLightR() / 255,
        self:GetLightG() / 255,
        self:GetLightB() / 255,
        true -- force update
    )
end

-- Property menu
function ENT:OpenPropertyMenu()
    if IsValid(self.PropertyPanel) then
        self.PropertyPanel:Remove()
    end

    local frame = vgui.Create("DFrame")
    frame:SetSize(300, 400)
    frame:SetTitle("RTX Light Properties")
    frame:MakePopup()
    frame:Center()
    
    local scroll = vgui.Create("DScrollPanel", frame)
    scroll:Dock(FILL)
    
    -- Brightness Slider
    local brightnessSlider = scroll:Add("DNumSlider")
    brightnessSlider:Dock(TOP)
    brightnessSlider:SetText("Brightness")
    brightnessSlider:SetMin(1)
    brightnessSlider:SetMax(1000)
    brightnessSlider:SetDecimals(0)
    brightnessSlider:SetValue(self:GetLightBrightness())
    brightnessSlider.OnValueChanged = function(_, value)
        net.Start("RTXLight_UpdateProperty")
            net.WriteEntity(self)
            net.WriteString("brightness")
            net.WriteFloat(value)
        net.SendToServer()
        self:OnPropertyChanged()
    end
    
    -- Size Slider
    local sizeSlider = scroll:Add("DNumSlider")
    sizeSlider:Dock(TOP)
    sizeSlider:SetText("Size")
    sizeSlider:SetMin(50)
    sizeSlider:SetMax(1000)
    sizeSlider:SetDecimals(0)
    sizeSlider:SetValue(self:GetLightSize())
    sizeSlider.OnValueChanged = function(_, value)
        net.Start("RTXLight_UpdateProperty")
            net.WriteEntity(self)
            net.WriteString("size")
            net.WriteFloat(value)
        net.SendToServer()
        self:OnPropertyChanged()
    end
    
    -- Color Mixer
    local colorMixer = scroll:Add("DColorMixer")
    colorMixer:Dock(TOP)
    colorMixer:SetTall(200)
    colorMixer:SetPalette(false)
    colorMixer:SetAlphaBar(false)
    colorMixer:SetColor(Color(self:GetLightR(), self:GetLightG(), self:GetLightB()))
    colorMixer.ValueChanged = function(_, color)
        net.Start("RTXLight_UpdateProperty")
            net.WriteEntity(self)
            net.WriteString("color")
            net.WriteUInt(color.r, 8)
            net.WriteUInt(color.g, 8)
            net.WriteUInt(color.b, 8)
        net.SendToServer()
        self:OnPropertyChanged()
    end
    
    self.PropertyPanel = frame
end

properties.Add("rtx_light_properties", {
    MenuLabel = "Edit RTX Light",
    Order = 1,
    MenuIcon = "icon16/lightbulb.png",
    
    Filter = function(self, ent, ply)
        return IsValid(ent) and ent:GetClass() == "base_rtx_light"
    end,
    
    Action = function(self, ent)
        ent:OpenPropertyMenu()
    end
})