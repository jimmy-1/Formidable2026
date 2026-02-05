#pragma once
#include <Windows.h>
#include <cstdint>

struct ModParam {
	uint32_t cid;
	uint32_t pid;
};

INT_PTR CALLBACK ModuleDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void ShowModuleDialog(HWND hParent, uint32_t clientId, uint32_t pid);
