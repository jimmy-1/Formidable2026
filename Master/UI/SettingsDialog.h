#pragma once
#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// 设置对话框
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

#endif // SETTINGSDIALOG_H
