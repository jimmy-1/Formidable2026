#pragma once
#ifndef BUILDERDIALOG_H
#define BUILDERDIALOG_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// 生成器对话框
INT_PTR CALLBACK BuilderDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

#endif // BUILDERDIALOG_H
