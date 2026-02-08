#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

INT_PTR CALLBACK ServiceDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
