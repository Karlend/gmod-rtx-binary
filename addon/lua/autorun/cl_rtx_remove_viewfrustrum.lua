local cv_disable_culling = CreateClientConVar("disable_frustum_culling", "1", true, false, "Disable frustum culling")
local cv_bounds_size = CreateClientConVar("frustum_bounds_size", "10000", true, false, "Size of render bounds when culling is disabled (default: 1000)")
local cv_update_frequency = CreateClientConVar("frustum_update_frequency", "0.5", true, false, "How often to update moving entities (in seconds)")
local cv_process_batch_size = CreateClientConVar("frustum_batch_size", "50", true, false, "How many entities to process per frame when refreshing")

-- Cache vectors to avoid creating new ones constantly
local cached_bounds_size = cv_bounds_size:GetFloat()
local cached_mins = Vector(-cached_bounds_size, -cached_bounds_size, -cached_bounds_size)
local cached_maxs = Vector(cached_bounds_size, cached_bounds_size, cached_bounds_size)

-- Processing queue for batch operations
local processing_queue = {}
local is_processing = false

-- Helper function to safely set render bounds
local function SetHugeRenderBounds(ent)
    if not IsValid(ent) then return end
    if not ent.SetRenderBounds then return end
    
    -- Only set bounds for visible entities
    if ent:GetNoDraw() then return end
    
    -- Check if entity is renderable (has a model)
    local model = ent:GetModel()
    if not model or model == "" then return end
    
    -- Distance check (only process entities within 20000 units of any player)
    local should_process = false
    local ent_pos = ent:GetPos()
    
    for _, ply in ipairs(player.GetAll()) do
        if ply:GetPos():DistToSqr(ent_pos) <= 20000 * 20000 then
            should_process = true
            break
        end
    end
    
    if not should_process then return end
    
    ent:SetRenderBounds(cached_mins, cached_maxs)
end

-- Batch processing function
local function ProcessBatch()
    if #processing_queue == 0 then
        is_processing = false
        return
    end
    
    local batch_size = math.min(cv_process_batch_size:GetInt(), #processing_queue)
    
    for i = 1, batch_size do
        local ent = table.remove(processing_queue, 1)
        if IsValid(ent) then
            SetHugeRenderBounds(ent)
        end
    end
    
    if #processing_queue > 0 then
        timer.Simple(0, ProcessBatch)
    else
        is_processing = false
    end
end

-- Function to queue entities for processing
local function QueueEntitiesForProcessing(entities)
    for _, ent in ipairs(entities) do
        table.insert(processing_queue, ent)
    end
    
    if not is_processing then
        is_processing = true
        ProcessBatch()
    end
end

-- Hook entity spawn
hook.Add("OnEntityCreated", "SetHugeRenderBounds", function(ent)
    if not IsValid(ent) then return end
    
    timer.Simple(0, function()
        if IsValid(ent) then
            SetHugeRenderBounds(ent)
        end
    end)
end)

-- Add command to toggle
concommand.Add("toggle_frustum_culling", function()
    cv_disable_culling:SetBool(not cv_disable_culling:GetBool())
    print("Frustum culling: " .. (cv_disable_culling:GetBool() and "DISABLED" or "ENABLED"))
end)

-- Add command to adjust bounds size
concommand.Add("set_frustum_bounds", function(ply, cmd, args)
    if not args[1] then 
        print("Current bounds size: " .. cv_bounds_size:GetFloat())
        return
    end
    
    local new_size = tonumber(args[1])
    if not new_size then
        print("Invalid size value. Please use a number.")
        return
    end
    
    cv_bounds_size:SetFloat(new_size)
    print("Set frustum bounds size to: " .. new_size)
    
    -- Queue all entities for processing
    QueueEntitiesForProcessing(ents.GetAll())
end)

-- Update cached vectors when bounds size changes
cvars.AddChangeCallback("frustum_bounds_size", function(name, old, new)
    cached_bounds_size = cv_bounds_size:GetFloat()
    cached_mins = Vector(-cached_bounds_size, -cached_bounds_size, -cached_bounds_size)
    cached_maxs = Vector(cached_bounds_size, cached_bounds_size, cached_bounds_size)
    
    -- Queue all entities for processing
    QueueEntitiesForProcessing(ents.GetAll())
end)

-- Monitor other convar changes
cvars.AddChangeCallback("disable_frustum_culling", function(name, old, new)
    QueueEntitiesForProcessing(ents.GetAll())
end)

-- Optimized think hook with timer-based updates
local next_update = 0
hook.Add("Think", "UpdateRenderBounds", function()
    if not cv_disable_culling:GetBool() then return end
    
    local curTime = CurTime()
    if curTime < next_update then return end
    
    next_update = curTime + cv_update_frequency:GetFloat()
    
    -- Only process dynamic entities that actually have bones
    local dynamic_entities = {}
    for _, ent in ipairs(ents.GetAll()) do
        if IsValid(ent) and 
           ent:GetMoveType() != MOVETYPE_NONE and
           ent:GetBoneCount() and ent:GetBoneCount() > 0 then
            table.insert(dynamic_entities, ent)
        end
    end
    
    QueueEntitiesForProcessing(dynamic_entities)
end)

-- Optimized bone setup hook
hook.Add("InitPostEntity", "SetupRenderBoundsOverride", function()
    local meta = FindMetaTable("Entity")
    if not meta then return end
    
    -- Store original function if it exists
    local originalSetupBones = meta.SetupBones
    if originalSetupBones then
        function meta:SetupBones()
            if cv_disable_culling:GetBool() and self:GetBoneCount() and self:GetBoneCount() > 0 then
                SetHugeRenderBounds(self)
            end
            return originalSetupBones(self)
        end
    end
end)

-- Debug command to print entity info
concommand.Add("debug_render_bounds", function()
    print("\nEntity Render Bounds Debug:")
    print("Current bounds size: " .. cv_bounds_size:GetFloat())
    print("Update frequency: " .. cv_update_frequency:GetFloat() .. " seconds")
    print("Batch size: " .. cv_process_batch_size:GetInt() .. " entities")
    print("Queue size: " .. #processing_queue .. " entities")
    print("\nEntity Details:")
    
    local total_entities = 0
    local processed_entities = 0
    local bone_entities = 0
    
    for _, ent in ipairs(ents.GetAll()) do
        if IsValid(ent) and ent.SetRenderBounds then
            total_entities = total_entities + 1
            local model = ent:GetModel() or "no model"
            local class = ent:GetClass()
            local pos = ent:GetPos()
            local has_bones = ent:GetBoneCount() and ent:GetBoneCount() > 0
            
            -- Check if entity is being processed (within distance)
            local should_process = false
            for _, ply in ipairs(player.GetAll()) do
                if ply:GetPos():DistToSqr(pos) <= 20000 * 20000 then
                    should_process = true
                    processed_entities = processed_entities + 1
                    break
                end
            end
            
            if has_bones then
                bone_entities = bone_entities + 1
            end
            
            print(string.format("Entity %s (Model: %s) - Distance processed: %s, Has bones: %s",
                class, model, should_process and "Yes" or "No", has_bones and "Yes" or "No"))
        end
    end
    
    print("\nStatistics:")
    print("Total entities: " .. total_entities)
    print("Entities in range: " .. processed_entities)
    print("Entities with bones: " .. bone_entities)
end)