#include "interfaces.h"

// Define the interfaces
IMaterialSystem* materials = nullptr;
IVEngineClient* engine = nullptr;
ICvar* g_pCVar = nullptr;  // Add this definition