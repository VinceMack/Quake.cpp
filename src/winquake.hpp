// winquake.h: Win32-specific Quake header file
#pragma once

#pragma warning(disable : 4229) // mgraph gets this

#include <windows.h>
#define WM_MOUSEWHEEL 0x020A

#ifndef SERVERONLY
// DirectX headers removed for clean SDL build
#endif

extern HINSTANCE global_hInstance;
extern int global_nCmdShow;

extern HWND mainwindow;
extern qboolean ActiveApp, Minimized;

extern qboolean WinNT;

int VID_ForceUnlockedAndReturnState(void);
void VID_ForceLockState(int lk);


extern qboolean winsock_lib_initialized;

extern cvar_t _windowed_mouse;

extern int window_center_x, window_center_y;
extern RECT window_rect;

extern qboolean mouseinitialized;
extern HWND hwnd_dialog;

extern HANDLE hinput, houtput;

void IN_UpdateClipCursor(void);
void CenterWindow(HWND hWndCenter, int width, int height, BOOL lefttopjustify);

void S_BlockSound(void);
void S_UnblockSound(void);

void VID_SetDefaultMode(void);


