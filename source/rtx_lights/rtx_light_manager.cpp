#include "rtx_light_manager.h"
#include <tier0/dbg.h>
#include <algorithm>
#include <thread>

RTXLightManager::RTXLightManager() 
    : m_remix(nullptr)
    , m_initialized(false)
    , m_running(false) {
}

RTXLightManager::~RTXLightManager() {
    Shutdown();
}

RTXLightManager& RTXLightManager::Instance() {
    static RTXLightManager instance;
    return instance;
}

void RTXLightManager::Initialize(remix::Interface* remixInterface) {
    std::lock_guard<std::mutex> lock(m_lightCS);
    m_remix = remixInterface;
    m_initialized = true;
    m_running = true;
    StartUpdateThread();
    LogMessage("RTX Light Manager initialized\n");
}

void RTXLightManager::StartUpdateThread() {
    m_updateThread = std::thread(&RTXLightManager::UpdateThread, this);
}

void RTXLightManager::UpdateThread() {
    while (m_running) {
        ProcessUpdateQueue();
        std::this_thread::sleep_for(std::chrono::milliseconds(
            static_cast<int>(UPDATE_INTERVAL * 1000)));
    }
}

void RTXLightManager::ProcessUpdateQueue() {
    std::vector<LightUpdateCommand> batchUpdates;
    
    {
        std::lock_guard<std::mutex> lock(m_queueCS);
        while (!m_updateQueue.empty() && batchUpdates.size() < MAX_QUEUE_SIZE) {
            batchUpdates.push_back(m_updateQueue.front());
            m_updateQueue.pop();
        }
    }

    for (const auto& cmd : batchUpdates) {
        ProcessSingleUpdate(cmd);
    }
}

void RTXLightManager::ProcessSingleUpdate(const LightUpdateCommand& cmd) {
    std::lock_guard<std::mutex> lock(m_lightCS);
    
    auto it = m_lights.find(cmd.handle);
    if (it == m_lights.end()) return;

    auto& state = it->second;
    auto now = std::chrono::steady_clock::now();
    
    // Skip update if too soon unless forced
    if (!cmd.forceUpdate) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.lastUpdateTime).count() / 1000.0f;
        if (elapsed < MIN_UPDATE_INTERVAL) return;
    }

    try {
        // Create new light info
        auto sphereLight = remixapi_LightInfoSphereEXT{};
        sphereLight.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
        sphereLight.position = {cmd.properties.x, cmd.properties.y, cmd.properties.z};
        sphereLight.radius = cmd.properties.size;
        sphereLight.shaping_hasvalue = false;

        auto lightInfo = remixapi_LightInfo{};
        lightInfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
        lightInfo.pNext = &sphereLight;
        lightInfo.hash = GenerateLightHash();
        lightInfo.radiance = {
            cmd.properties.r * cmd.properties.brightness,
            cmd.properties.g * cmd.properties.brightness,
            cmd.properties.b * cmd.properties.brightness
        };

        // Create new light
        auto result = m_remix->CreateLight(lightInfo);
        if (!result) {
            LogMessage("Failed to create new light during update\n");
            return;
        }

        // Destroy old light
        if (state.handle) {
            m_remix->DestroyLight(state.handle);
        }

        // Update state
        state.handle = result.value();
        state.properties = cmd.properties;
        state.lastUpdateTime = now;
        state.hash = lightInfo.hash;
        state.needsUpdate = false;
    }
    catch (...) {
        LogMessage("Exception in ProcessSingleUpdate\n");
    }
}

void RTXLightManager::QueueLightUpdate(remixapi_LightHandle handle, 
    const LightProperties& props, bool force) {
    
    if (!ValidateLightProperties(props)) {
        LogMessage("Invalid light properties in update request\n");
        return;
    }

    LightUpdateCommand cmd{handle, props, force};
    
    std::lock_guard<std::mutex> lock(m_queueCS);
    if (m_updateQueue.size() < MAX_QUEUE_SIZE) {
        m_updateQueue.push(cmd);
    }
}

bool RTXLightManager::ValidateLightProperties(const LightProperties& props) const {
    // Basic validation
    if (props.size <= 0 || props.brightness < 0) return false;
    if (props.r < 0 || props.r > 1 || 
        props.g < 0 || props.g > 1 || 
        props.b < 0 || props.b > 1) return false;
    
    return true;
}

remixapi_LightHandle RTXLightManager::CreateLight(const LightProperties& props) {
    if (!m_initialized || !m_remix) return nullptr;
    
    std::lock_guard<std::mutex> lock(m_lightCS);
    
    try {
        auto sphereLight = remixapi_LightInfoSphereEXT{};
        sphereLight.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
        sphereLight.position = {props.x, props.y, props.z};
        sphereLight.radius = props.size;
        sphereLight.shaping_hasvalue = false;
        memset(&sphereLight.shaping_value, 0, sizeof(sphereLight.shaping_value));

        uint64_t hash = GenerateLightHash();
        
        auto lightInfo = remixapi_LightInfo{};
        lightInfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
        lightInfo.pNext = &sphereLight;
        lightInfo.hash = hash;
        lightInfo.radiance = {
            props.r * props.brightness,
            props.g * props.brightness,
            props.b * props.brightness
        };

        auto result = m_remix->CreateLight(lightInfo);
        if (!result) {
            return nullptr;
        }

        LightState state{};
        state.handle = result.value();
        state.properties = props;
        state.lastUpdateTime = std::chrono::steady_clock::now();
        state.needsUpdate = false;
        state.hash = hash;
        state.active = true;

        // Use insert for unordered_map
        m_lights[state.handle] = state;
        
        return state.handle;
    }
    catch (...) {
        Msg("[RTX Light Manager] Exception in CreateLight\n");
    }
    
    return nullptr;
}

// Add a method to generate unique hashes
uint64_t RTXLightManager::GenerateLightHash() const {
    static uint64_t counter = 0;
    return (static_cast<uint64_t>(GetCurrentProcessId()) << 32) | (++counter);
}

bool RTXLightManager::UpdateLight(remixapi_LightHandle handle, const LightProperties& props) {
    if (!m_initialized || !m_remix) return false;

    std::lock_guard<std::mutex> lock(m_lightCS);

    try {
        auto it = m_lights.find(handle);
        if (it != m_lights.end()) {
            // Create new light info
            auto sphereLight = remixapi_LightInfoSphereEXT{};
            sphereLight.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
            sphereLight.position = {props.x, props.y, props.z};
            sphereLight.radius = props.size;
            sphereLight.shaping_hasvalue = false;
            memset(&sphereLight.shaping_value, 0, sizeof(sphereLight.shaping_value));

            auto lightInfo = remixapi_LightInfo{};
            lightInfo.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
            lightInfo.pNext = &sphereLight;
            lightInfo.hash = GenerateLightHash();
            lightInfo.radiance = {
                props.r * props.brightness,
                props.g * props.brightness,
                props.b * props.brightness
            };

            // Create new light
            auto result = m_remix->CreateLight(lightInfo);
            if (!result) {
                LogMessage("Failed to create new light during update\n");
                return false;
            }

            // Destroy old light
            if (it->second.handle) {
                m_remix->DestroyLight(it->second.handle);
            }

            // Update state
            it->second.handle = result.value();
            it->second.properties = props;
            it->second.lastUpdateTime = std::chrono::steady_clock::now();
            it->second.needsUpdate = false;
            it->second.hash = lightInfo.hash;

            return true;
        }
    }
    catch (...) {
        LogMessage("Exception in UpdateLight\n");
    }

    return false;
}

void RTXLightManager::DestroyLight(remixapi_LightHandle handle) {
    if (!m_initialized || !m_remix) return;

    std::lock_guard<std::mutex> lock(m_lightCS);

    try {
        auto it = m_lights.find(handle);
        if (it != m_lights.end()) {
            LogMessage("Destroying light handle: %p\n", handle);
            m_remix->DestroyLight(it->second.handle);
            m_lights.erase(it);
            LogMessage("Light destroyed, remaining lights: %d\n", m_lights.size());
        }
    }
    catch (...) {
        LogMessage("Exception in DestroyLight\n");
    }
}

void RTXLightManager::DrawLights() {
    if (!m_initialized || !m_remix) return;

    std::lock_guard<std::mutex> lock(m_lightCS);
    
    try {
        // Only print debug info every few seconds and if the light count changed
        static size_t lastLightCount = 0;
        static float lastDebugTime = 0;
        float currentTime = GetTickCount64() / 1000.0f;
        
        if (m_lights.size() != lastLightCount && currentTime - lastDebugTime > 2.0f) {
            Msg("[RTX Light Manager] Drawing %d lights\n", m_lights.size());
            lastLightCount = m_lights.size();
            lastDebugTime = currentTime;
        }

        for (const auto& pair : m_lights) {
            if (pair.second.handle) {
                auto result = m_remix->DrawLightInstance(pair.second.handle);
                if (!result && currentTime - lastDebugTime > 2.0f) {
                    Msg("[RTX Light Manager] Failed to draw light handle: %p\n", pair.second.handle);
                }
            }
        }
    }
    catch (...) {
        LogMessage("Exception in DrawLights\n");
    }
}

void RTXLightManager::Shutdown() {
    m_running = false;
    if (m_updateThread.joinable()) {
        m_updateThread.join();
    }

    std::lock_guard<std::mutex> lock(m_lightCS);
    
    for (const auto& pair : m_lights) {
        if (pair.second.handle) {
            m_remix->DestroyLight(pair.second.handle);
        }
    }
    m_lights.clear();
    m_initialized = false;
    m_remix = nullptr;
}

void RTXLightManager::LogMessage(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Msg("[RTX Light Manager] %s", buffer);
}