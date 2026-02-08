#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>

INT_PTR CALLBACK KeylogDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
