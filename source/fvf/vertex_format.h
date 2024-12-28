#pragma once

// Only define if not already defined in Source SDK
#if !defined(VERTEX_FORMAT_T_DEFINED) && !defined(VERTEX_FORMAT_T)
#define VERTEX_FORMAT_T_DEFINED
#define VERTEX_FORMAT_T
#endif

// Vertex format flags
#define FF_VERTEX_POSITION      0x0001u
#define FF_VERTEX_NORMAL        0x0002u
#define FF_VERTEX_COLOR         0x0004u
#define FF_VERTEX_SPECULAR      0x0008u
#define FF_VERTEX_TEXCOORD0     0x0010u
#define FF_VERTEX_TEXCOORD1     0x0020u
#define FF_VERTEX_TEXCOORD2     0x0040u
#define FF_VERTEX_TEXCOORD3     0x0080u
#define FF_VERTEX_TEXCOORD4     0x0100u
#define FF_VERTEX_TEXCOORD5     0x0200u
#define FF_VERTEX_TEXCOORD6     0x0400u
#define FF_VERTEX_TEXCOORD7     0x0800u
#define FF_VERTEX_TANGENT       0x1000u
#define FF_VERTEX_BONE_WEIGHTS  0x2000u
#define FF_VERTEX_BONE_INDEX    0x4000u
#define FF_VERTEX_BONES        0x8000u  // For skinned meshes
#define FF_VERTEX_BONEWEIGHT   0x10000u // For skinned meshes
#define FF_VERTEX_USERDATA     0x20000u // For additional vertex data
#define FF_VERTEX_MODEL        0x40000u // For model-specific data