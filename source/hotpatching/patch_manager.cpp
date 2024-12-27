#include "patch_manager.h"
#include <tier0/dbg.h>
#include "../e_utils.h"
#include <sstream>

// Helper function to convert hex string to bytes
std::vector<uint8_t> HexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        if (hex[i] == '?' && hex[i + 1] == '?') {
            bytes.push_back(0x00);  // Wildcard byte
        } else {
            std::string byteString = hex.substr(i, 2);
            if (byteString[0] == '?') byteString[0] = '0';
            if (byteString[1] == '?') byteString[1] = '0';
            bytes.push_back(static_cast<uint8_t>(std::stoul(byteString, nullptr, 16)));
        }
    }
    return bytes;
}

bool IsSectionExecutable(DWORD characteristics) {
    return (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
}

std::vector<std::pair<void*, size_t>> GetExecutableSections(HMODULE module) {
    std::vector<std::pair<void*, size_t>> sections;
    
    // Get DOS header
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)module;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return sections;

    // Get NT headers
    IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((uint8_t*)module + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return sections;

    // Get first section header
    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(ntHeaders);
    
    // Iterate through all sections
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        if (IsSectionExecutable(section[i].Characteristics)) {
            void* sectionStart = (uint8_t*)module + section[i].VirtualAddress;
            size_t sectionSize = section[i].Misc.VirtualSize;
            
            Msg("[Patch Manager] Found executable section: %s at %p, size: %zu\n",
                (char*)section[i].Name, sectionStart, sectionSize);
            
            sections.push_back({sectionStart, sectionSize});
        }
    }
    
    return sections;
}

void DumpExecutableSections(HMODULE module) {
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)module;
    IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((uint8_t*)module + dosHeader->e_lfanew);
    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(ntHeaders);
    
    Msg("[Patch Manager] Module sections:\n");
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        Msg("  Section: %-8.8s\n", (char*)section[i].Name);
        Msg("    VirtualAddress:  %08X\n", section[i].VirtualAddress);
        Msg("    VirtualSize:     %08X\n", section[i].Misc.VirtualSize);
        Msg("    Characteristics: %08X\n", section[i].Characteristics);
        
        // Dump first few bytes of executable sections
        if (IsSectionExecutable(section[i].Characteristics)) {
            void* sectionStart = (uint8_t*)module + section[i].VirtualAddress;
            Msg("    First bytes:     ");
            for (int j = 0; j < 16; j++) {
                Msg("%02X ", ((uint8_t*)sectionStart)[j]);
            }
            Msg("\n");
        }
    }
}

bool PatchManager::Initialize() {
    Msg("[Patch Manager] Initializing patches...\n");

    // Engine patches with more flexible patterns
    AddPatch("engine.dll", "75 ?? F3 0F 10", 0, {0xEB}); // brush entity backfaces
    AddPatch("engine.dll", "7E ?? 44 ?? ??", 0, {0xEB}); // world backfaces
    AddPatch("engine.dll", "75 ?? 49 8B 42", 0, {0xEB}); // world backfaces

    // ShaderAPI patches with more flexible patterns - Disabled some for now, we handle these differently now
    AddPatch("shaderapidx9.dll", "48 0F 4E ?? C7", 0, {0x90, 0x90, 0x90, 0x90}); // four hardware lights
//    AddPatch("shaderapidx9.dll", "48 33 CC E8 ?? ?? ?? ?? 48 81 C4 48", 0, {0x85, 0xC0, 0x75, 0x04, 0x66, 0xB8, 0x04, 0x00}); // zero sized buffer
//    AddPatch("shaderapidx9.dll", "48 83 EC 08 4C ?? ??", 0, {0x31, 0xC0, 0xC3}); // shader constants

    // Client patches with more flexible patterns
//    AddPatch("client.dll", "48 83 EC 48 0F 10", 0, {0x31, 0xC0, 0xC3}); // c_frustumcull
//    AddPatch("client.dll", "0F B6 81 54 ?? ??", 0, {0xB0, 0x01, 0xC3}); // r_forcenovis

    // Datacache patches with more flexible patterns
    AddPatch("datacache.dll", "64 78 38 30 ?? 76 74 78", 0, {0x64, 0x78, 0x39, 0x30, 0x2E, 0x76, 0x74, 0x78}); // force load dx9 vtx

    return true;
}

void PatchManager::AddPatch(const std::string& moduleName, const std::string& pattern, 
                          size_t offset, const std::vector<uint8_t>& replacement) {
    BinaryPatch patch;
    patch.pattern = pattern;
    patch.offset = offset;
    patch.replacement = replacement;
    m_patches.emplace_back(moduleName, patch);
}

bool PatchManager::ApplyPatches() {
    bool success = true;
    for (auto& pair : m_patches) {
        const std::string& moduleName = pair.first;
        BinaryPatch& patch = pair.second;
        if (!ApplyPatchToModule(moduleName, patch)) {
            Warning("[Patch Manager] Failed to apply patch to %s\n", moduleName.c_str());
            success = false;
        }
    }
    return success;
}

bool PatchManager::RestorePatches() {
    bool success = true;
    for (auto& pair : m_patches) {
        const std::string& moduleName = pair.first;
        BinaryPatch& patch = pair.second;
        if (patch.address && !patch.original.empty()) {
            if (!WriteMemory(patch.address, patch.original)) {
                Warning("[Patch Manager] Failed to restore patch in %s\n", moduleName.c_str());
                success = false;
            }
        }
    }
    return success;
}

bool PatchManager::ApplyPatchToModule(const std::string& moduleName, BinaryPatch& patch) {
    HMODULE module = GetModuleHandleA(moduleName.c_str());
    if (!module) {
        Warning("[Patch Manager] Could not find module %s\n", moduleName.c_str());
        return false;
    }

    DumpExecutableSections(module);

    MODULEINFO modInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &modInfo, sizeof(MODULEINFO))) {
        Warning("[Patch Manager] Failed to get module info for %s\n", moduleName.c_str());
        return false;
    }

    Msg("[Patch Manager] Scanning %s (base: %p, size: %u) for pattern: %s\n",
        moduleName.c_str(), module, modInfo.SizeOfImage, patch.pattern.c_str());

    // Convert pattern to proper format
    std::string cleanPattern = patch.pattern;
    cleanPattern.erase(std::remove(cleanPattern.begin(), cleanPattern.end(), ' '), cleanPattern.end());

    std::vector<uint8_t> patternBytes = HexToBytes(cleanPattern);
    void* address = ScanMemoryRegion(module, modInfo.SizeOfImage, patternBytes);

    if (!address) {
        Warning("[Patch Manager] Pattern not found in %s\n", moduleName.c_str());
        // Only dump a small region around where we expect to find the pattern
        DumpMemoryRegion(module, 4096); // Dump first page as example
        return false;
    }

    address = (uint8_t*)address + patch.offset;
    patch.original = ReadMemory(address, patch.replacement.size());
    patch.address = address;

    Msg("[Patch Manager] Found pattern at %p\n", address);
    if (!WriteMemory(address, patch.replacement)) {
        Warning("[Patch Manager] Failed to write patch to %s at %p\n", moduleName.c_str(), address);
        return false;
    }

    Msg("[Patch Manager] Successfully applied patch to %s at %p\n", moduleName.c_str(), address);
    return true;
}

void* PatchManager::ScanMemoryRegion(void* start, size_t size, const std::vector<uint8_t>& pattern) {
    uint8_t* current = (uint8_t*)start;
    uint8_t* end = current + size - pattern.size();
    size_t matches = 0;
    
    // Add more detailed debug output
    Msg("[Patch Manager] Pattern bytes: ");
    for (auto byte : pattern) {
        Msg("%02X ", byte);
    }
    Msg("\n");

    while (current < end) {
        bool found = true;
        for (size_t i = 0; i < pattern.size(); i++) {
            // Skip wildcard bytes (0x00 in our pattern represents wildcard)
            if (pattern[i] == 0x00) continue;
            
            if (pattern[i] != current[i]) {
                found = false;
                break;
            }
        }
        
        if (found) {
            // Log the full context of the match
            Msg("[Patch Manager] Potential match at %p. Context:\n", current);
            
            // Print 32 bytes before
            Msg("Before: ");
            for (int i = -32; i < 0; i++) {
                if (current + i >= (uint8_t*)start) {
                    Msg("%02X ", current[i]);
                }
            }
            Msg("\n");
            
            // Print the matching bytes
            Msg("Match:  ");
            for (size_t i = 0; i < pattern.size(); i++) {
                Msg("%02X ", current[i]);
            }
            Msg("\n");
            
            // Print 32 bytes after
            Msg("After:  ");
            for (size_t i = pattern.size(); i < pattern.size() + 32; i++) {
                if (current + i < end) {
                    Msg("%02X ", current[i]);
                }
            }
            Msg("\n");
            
            matches++;
            return current;
        }
        current++;
    }

    Msg("[Patch Manager] No matches found in region. Scanned %zu bytes\n", size);
    return nullptr;
}

void PatchManager::DumpMemoryRegion(void* start, size_t totalSize, size_t dumpSize) {
    Msg("[Patch Manager] Memory dump around %p:\n", start);
    
    // Get module information
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(start, &mbi, sizeof(mbi))) {
        Msg("[Patch Manager] Region info:\n");
        Msg("  Base address: %p\n", mbi.BaseAddress);
        Msg("  Region size: %zu\n", mbi.RegionSize);
        Msg("  Protection: %08X\n", mbi.Protect);
        Msg("  State: %08X\n", mbi.State);
        Msg("  Type: %08X\n", mbi.Type);
    }

    uint8_t* current = (uint8_t*)start;
    size_t size = (dumpSize < totalSize) ? dumpSize : totalSize;
    
    // Dump in both hex and ASCII format
    char hexBuffer[256];
    char asciiBuffer[17];
    for (size_t i = 0; i < size; i += 16) {
        int offset = sprintf_s(hexBuffer, sizeof(hexBuffer), "%p: ", (void*)(current + i));
        
        // Hex values
        for (size_t j = 0; j < 16 && (i + j) < size; j++) {
            offset += sprintf_s(hexBuffer + offset, sizeof(hexBuffer) - offset, 
                              "%02X ", current[i + j]);
                              
            // ASCII representation
            asciiBuffer[j] = (current[i + j] >= 32 && current[i + j] <= 126) 
                           ? (char)current[i + j] : '.';
        }
        
        // Pad with spaces if needed
        while (offset < 58) hexBuffer[offset++] = ' ';
        
        // Add ASCII representation
        asciiBuffer[16] = '\0';
        sprintf_s(hexBuffer + offset, sizeof(hexBuffer) - offset, "| %s", asciiBuffer);
        
        Msg("%s\n", hexBuffer);
    }
}

bool PatchManager::WriteMemory(void* address, const std::vector<uint8_t>& bytes) {
    DWORD oldProtect;
    if (!VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    memcpy(address, bytes.data(), bytes.size());

    DWORD dummy;
    VirtualProtect(address, bytes.size(), oldProtect, &dummy);
    return true;
}

std::vector<uint8_t> PatchManager::ReadMemory(void* address, size_t size) {
    std::vector<uint8_t> bytes(size);
    DWORD oldProtect;

    if (VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(bytes.data(), address, size);
        DWORD dummy;
        VirtualProtect(address, size, oldProtect, &dummy);
    }

    return bytes;
}