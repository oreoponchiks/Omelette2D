#pragma once

/* The pinned upstream cimgui_impl.h does not include Win32. These declarations
   expose the existing backend functions using cimgui's C-linkage convention. */
#include "cimgui.h"
#include <windows.h>

CIMGUI_API bool ImGui_ImplWin32_Init(void* hwnd);
CIMGUI_API void ImGui_ImplWin32_Shutdown(void);
CIMGUI_API void ImGui_ImplWin32_NewFrame(void);
CIMGUI_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam,
                                                  LPARAM lparam);
