//

#pragma once

#include "targetver.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#define WM_TRAYICON (WM_USER + 1)
#define WM_MINIMIZE_TO_TRAY (WM_USER + 2)
#define WM_DELETE_TRAY (WM_USER + 3)
