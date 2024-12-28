#pragma once
#include <tier0/dbg.h>

// Debug helpers - shared across all FVF files
#define FF_LOG(fmt, ...) Msg("[Fixed Function] " fmt "\n", ##__VA_ARGS__)
#define FF_WARN(fmt, ...) Warning("[Fixed Function] " fmt "\n", ##__VA_ARGS__)