#pragma once
#include <vector>
#include <string>
#include <Windows.h>
#include <Psapi.h>

struct BinaryPatch {
    std::string pattern;
    size_t offset;
    std::vector<uint8_t> replacement;
    std::vector<uint8_t> original;  // Store original bytes for restoration
    void* address;
};

class PatchManager {
public:
    static PatchManager& Instance() {
        static PatchManager instance;
        return instance;
    }

    static std::vector<uint8_t> FormatPattern(const std::string& pattern) {
    std::vector<uint8_t> result;
    std::string temp = pattern;
    
    // Remove spaces
    temp.erase(std::remove(temp.begin(), temp.end(), ' '), temp.end());
    
    // Convert to bytes
    for (size_t i = 0; i < temp.length(); i += 2) {
        if (temp[i] == '?' && temp[i + 1] == '?') {
            result.push_back(0x00);  // Wildcard
            continue;
        }
        
        uint8_t byte = (uint8_t)std::stoul(temp.substr(i, 2), nullptr, 16);
        result.push_back(byte);
    }
    
    return result;
    }

    bool Initialize();
    bool ApplyPatches();
    bool RestorePatches();
    void AddPatch(const std::string& moduleName, const std::string& pattern, 
                 size_t offset, const std::vector<uint8_t>& replacement);

private:
    PatchManager() = default;
    ~PatchManager() { RestorePatches(); }

    bool ApplyPatchToModule(const std::string& moduleName, BinaryPatch& patch);
    void* ScanMemoryRegion(void* start, size_t size, const std::vector<uint8_t>& pattern);
    void DumpMemoryRegion(void* start, size_t totalSize, size_t dumpSize = 256);
    bool WriteMemory(void* address, const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> ReadMemory(void* address, size_t size);

    std::vector<std::pair<std::string, BinaryPatch>> m_patches;
    DWORD m_oldProtect;
};