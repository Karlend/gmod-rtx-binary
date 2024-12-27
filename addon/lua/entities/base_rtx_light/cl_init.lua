include("shared.lua")

ENT.HasInitializedLight = false
ENT.AttemptCount = 0
ENT.MaxAttempts = 3

function ENT:Initialize()
    self:SetNoDraw(true)
    self:DrawShadow(false)
    self.HasInitializedLight = false
    self.AttemptCount = 0
    
    -- Delay light creation to ensure networked values are received
    timer.Simple(0.1, function()
        if IsValid(self) and not self.HasInitializedLight then
            self:CreateRTXLight()
        end
    end)
end

function ENT:CreateRTXLight()
    -- Prevent multiple creation attempts
    if self.HasInitializedLight then return end
    
    -- Increment attempt counter
    self.AttemptCount = self.AttemptCount + 1
    if self.AttemptCount > self.MaxAttempts then
        ErrorNoHalt(string.format("[RTX Light] Max creation attempts reached for entity: %d\n", self:EntIndex()))
        return
    end

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

    -- Validate values before creation
    if brightness <= 0 or size <= 0 then
        return
    end

    print(string.format("[RTX Light Entity] Creating light - Pos: %.2f,%.2f,%.2f, Size: %f, Brightness: %f, Color: %d,%d,%d",
        pos.x, pos.y, pos.z, size, brightness, r, g, b))

    local handle = CreateRTXLight(
        pos.x, 
        pos.y, 
        pos.z,
        size,
        brightness,
        r,
        g,
        b
    )

    if handle and handle > 0 then
        self.rtxLightHandle = handle
        self.lastUpdatePos = pos
        self.lastUpdateTime = CurTime()
        self.HasInitializedLight = true
        print("[RTX Light] Successfully created light with handle:", handle)
    else
        ErrorNoHalt("[RTX Light] Failed to create light\n")
    end
end

function ENT:Think()
    if not self.HasInitializedLight then
        -- Only try to create light once values are networked
        if self:GetLightBrightness() > 0 and self:GetLightSize() > 0 then
            self:CreateRTXLight()
        end
        return
    end

    if not self.nextUpdate then self.nextUpdate = 0 end
    if CurTime() < self.nextUpdate then return end
    
    -- Only update if we have a valid light
    if self.rtxLightHandle and self.rtxLightHandle > 0 then
        local pos = self:GetPos()
        
        -- Check if we actually need to update
        if not self.lastUpdatePos or pos:DistToSqr(self.lastUpdatePos) > 1 then
            local newHandle = UpdateRTXLight(
                self.rtxLightHandle,
                pos.x, pos.y, pos.z,
                self:GetLightSize(),
                self:GetLightBrightness(),
                self:GetLightR(),
                self:GetLightG(),
                self:GetLightB()
            )
            
            if newHandle and newHandle > 0 then
                self.rtxLightHandle = newHandle
                self.lastUpdatePos = pos
                self.lastUpdateTime = CurTime()
            else
                -- If update failed, try to recreate the light
                self.HasInitializedLight = false
                self.AttemptCount = 0
                self.rtxLightHandle = nil
            end
        end
    else
        -- Try to recreate light if handle is invalid
        self.HasInitializedLight = false
        self.AttemptCount = 0
        self.rtxLightHandle = nil
    end
    
    self.nextUpdate = CurTime() + 0.1  -- Update every 0.1 seconds
end

function ENT:OnRemove()
    if self.rtxLightHandle and self.rtxLightHandle > 0 then
        print("[RTX Light] Cleaning up light handle:", self.rtxLightHandle)
        pcall(function()
            DestroyRTXLight(self.rtxLightHandle)
        end)
        self.rtxLightHandle = nil
    end
    self.HasInitializedLight = false
end

-- Property menu implementation
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
    end
    
    self.PropertyPanel = frame
end

-- Network cleanup handler
net.Receive("RTXLight_Cleanup", function()
    local ent = net.ReadEntity()
    if IsValid(ent) then
        ent:OnRemove()
    end
end)

-- Property menu registration
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