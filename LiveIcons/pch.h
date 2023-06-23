// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include <objbase.h>
#include <shlwapi.h>
#include <shlobj.h>     // For SHChangeNotify
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <new>
#include <fstream>
#include <format>
#include <algorithm>
#include <corecrt_io.h>
#include <io.h>
#include <fcntl.h>
#include <cstdio>
#include <thumbcache.h> // For IThumbnailProvider.
#include <wincodec.h>
#include <wincrypt.h>
#include "framework.h"
#include "ReferenceCounter.h"

#include <system_error>

#endif //PCH_H
