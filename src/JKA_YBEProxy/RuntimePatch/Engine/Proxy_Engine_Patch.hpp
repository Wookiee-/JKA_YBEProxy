#pragma once

// Hooking addresses in Proxy_Engine_Wrappers.hpp are 32-bit x86 only
// (absolute stock-engine addresses). Never plant on other architectures:
// the tables hold no valid addresses there and patching would corrupt memory.
// Silent by design (no load-time logs).
#if defined(__i386__) || defined(_M_IX86)
#define PROXY_HOOKING_SUPPORTED 1
#else
#define PROXY_HOOKING_SUPPORTED 0
#endif

void Proxy_Engine_Attach_Patches(void);
void Proxy_Engine_Detach_Patches(void);
void Proxy_Engine_Inline_Patches(void);