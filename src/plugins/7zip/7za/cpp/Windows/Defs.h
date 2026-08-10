// Windows/Defs.h

#ifndef __WINDOWS_DEFS_H
#define __WINDOWS_DEFS_H

#include "WinDefs.h"

// Keep the host-only result helper without duplicating conversions now provided by 26.02 WinDefs.h.
#ifdef _WIN32
inline bool LRESULTToBool(LRESULT v) { return (v != FALSE); }
#endif

#endif
