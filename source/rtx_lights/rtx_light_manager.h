#pragma once
#include <d3d9.h>
#include "../../public/include/remix/remix.h"
#include "../../public/include/remix/remix_c.h"
#include <remix/remix_c.h>
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <Windows.h>

class RTXLightManager {
public:
    struct LightProperties {
        float x, y, z;          // Position
        float size;             // Light radius
        float brightness;       // Light intensity
        float r, g, b;         // Color (0-1 range)
    };

    // Internal state tracking
    struct LightState {
        remixapi_LightHandle handle;
        LightProperties properties;
        bool needsUpdate;
        std::chrono::steady_clock::time_point lastUpdateTime;
        uint64_t hash;
        bool active;
    };

    // Update command structure
    struct LightUpdateCommand {
        remixapi_LightHandle handle;
        LightProperties properties;
        bool forceUpdate;
    };

    static RTXLightManager& Instance();

    // Public interface
    void Initialize(remix::Interface* remixInterface);
    void Shutdown();
    remixapi_LightHandle CreateLight(const LightProperties& props);
    bool UpdateLight(remixapi_LightHandle handle, const LightProperties& props);
    void DestroyLight(remixapi_LightHandle handle);
    void DrawLights();

    // New batch processing methods
    void QueueLightUpdate(remixapi_LightHandle handle, const LightProperties& props, bool force = false);
    void ProcessUpdateQueue();
    size_t GetLightCount() const { return m_lights.size(); }
    
private:
    RTXLightManager();
    ~RTXLightManager();

    // Prevent copying
    RTXLightManager(const RTXLightManager&) = delete;
    RTXLightManager& operator=(const RTXLightManager&) = delete;

    // Internal helpers
    uint64_t GenerateLightHash() const;
    void ProcessSingleUpdate(const LightUpdateCommand& cmd);
    bool ValidateLightProperties(const LightProperties& props) const;
    void LogMessage(const char* format, ...);
    void CleanupInvalidLights();
    
    // Update management
    void StartUpdateThread();
    void StopUpdateThread();
    void UpdateThread();
    
    // Internal state
    remix::Interface* m_remix;
    std::unordered_map<remixapi_LightHandle, LightState> m_lights;
    std::queue<LightUpdateCommand> m_updateQueue;
    
    // Threading
    std::mutex m_lightCS;
    std::mutex m_queueCS;
    std::atomic<bool> m_running;
    std::thread m_updateThread;
    
    // Configuration
    static constexpr float UPDATE_INTERVAL = 1.0f / 60.0f; // 60hz updates
    static constexpr size_t MAX_QUEUE_SIZE = 1000;
    static constexpr float MIN_UPDATE_INTERVAL = 0.016f; // ~60fps
    
    bool m_initialized;
};