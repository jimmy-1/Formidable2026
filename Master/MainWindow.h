#pragma once
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <windows.h>
#include <string>

// 自定义消息
#define WM_CLIENT_DISCONNECT (WM_USER + 200)

// 主窗口函数声明
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void CreateMainMenu(HWND hWnd);
void CreateMainToolbar(HWND hWnd);
void UpdateStatusBar();
void InitListView(HWND hList);
void AddLog(const std::wstring& type, const std::wstring& msg);
void ShowAboutDialog(HWND hWnd);
int GetSelectedClientId();
void HandleCommand(HWND hWnd, int id);

// 工具函数
void EnsureStartupEnabled();
void RestartMaster(HWND hWnd);
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon(HWND hWnd);

#endif // MAINWINDOW_H
