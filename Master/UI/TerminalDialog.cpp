#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
// TerminalDialog.cpp - 终端对话框实现
#include "TerminalDialog.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include "../Utils/StringHelper.h"
#include "../GlobalState.h"
#include <CommCtrl.h>
#include <vector>
#include <map>
#include <mutex>

extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern HINSTANCE g_hInstance;
extern HFONT g_hTermFont;
extern HBRUSH g_hTermEditBkBrush;
extern bool SendDataToClient(std::shared_ptr<Formidable::ConnectedClient> client, const void* pData, int iLength);

namespace Formidable {
namespace UI {

struct TerminalState {
    uint32_t clientId;
    std::vector<std::wstring> history;
    int historyIndex = -1;
    std::wstring currentInput;
    int lastOutputEnd = 0; // 记录上次输出结束的位置，用户只能在此之后输入
};

static std::map<HWND, std::shared_ptr<TerminalState>> s_dlgStates;

// 终端输出框子类化过程 - 实现直接输入
LRESULT CALLBACK TerminalOutEditSubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    HWND hDlg = (HWND)dwRefData;
    auto it = s_dlgStates.find(hDlg);
    if (it == s_dlgStates.end()) return DefSubclassProc(hWnd, message, wParam, lParam);
    auto state = it->second;

    switch (message) {
    case WM_GETDLGCODE:
        if (lParam && ((LPMSG)lParam)->message == WM_KEYDOWN) {
            if (wParam == VK_RETURN || wParam == VK_BACK || wParam == VK_TAB)
                return DLGC_WANTALLKEYS;
        }
        break;

    case WM_CHAR: {
        // 阻止在输出结束位置之前输入
        int start, end;
        SendMessageW(hWnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        if (start < state->lastOutputEnd) {
            // 如果光标在受保护区域，移动到末尾再处理
            SendMessageW(hWnd, EM_SETSEL, -1, -1);
        }
        
        if (wParam == VK_BACK) {
            if (start <= state->lastOutputEnd) return 0; // 阻止删除输出内容
        }
        
        // 允许正常的字符输入
        return DefSubclassProc(hWnd, message, wParam, lParam);
    }

    case WM_KEYDOWN: {
        int start, end;
        SendMessageW(hWnd, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);

        if (wParam == VK_RETURN) {
            // 获取用户输入的命令（从 lastOutputEnd 到末尾）
            int len = GetWindowTextLengthW(hWnd);
            if (len > state->lastOutputEnd) {
                std::vector<wchar_t> buf(len + 1);
                GetWindowTextW(hWnd, buf.data(), len + 1);
                std::wstring wcmd = &buf[state->lastOutputEnd];
                
                // 清理换行符
                while (!wcmd.empty() && (wcmd.back() == L'\r' || wcmd.back() == L'\n')) wcmd.pop_back();

                if (!wcmd.empty()) {
                    // 添加到历史
                    if (state->history.empty() || state->history.back() != wcmd) {
                        state->history.push_back(wcmd);
                        if (state->history.size() > 100) state->history.erase(state->history.begin());
                    }
                    state->historyIndex = -1;

                    // 发送给客户端
                    std::shared_ptr<Formidable::ConnectedClient> client;
                    {
                        std::lock_guard<std::mutex> lock(g_ClientsMutex);
                        if (g_Clients.count(state->clientId)) client = g_Clients[state->clientId];
                    }
                    if (client) {
                        std::string cmd = Formidable::Utils::StringHelper::WideToUTF8(wcmd + L"\n");
                        Formidable::CommandPkg pkg = { 0 };
                        pkg.cmd = Formidable::CMD_TERMINAL_DATA;
                        pkg.arg1 = (uint32_t)cmd.length();

                        // 修正：计算正确的 bodySize 和偏移量
                        // CommandPkg 的 data[1] 是变长数据的起始位置
                        size_t headerSize = offsetof(Formidable::CommandPkg, data);
                        size_t bodySize = headerSize + cmd.length();
                        
                        std::vector<char> sendBuf(sizeof(Formidable::PkgHeader) + bodySize);
                        Formidable::PkgHeader* h = (Formidable::PkgHeader*)sendBuf.data();
                        memcpy(h->flag, "FRMD26?", 7);
                        h->originLen = (int)bodySize;
                        h->totalLen = (int)sendBuf.size();
                        
                        // 先拷贝头部（cmd, arg1, arg2）
                        memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader), &pkg, headerSize);
                        // 再拷贝数据到 data 字段开始的位置
                        memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader) + headerSize, cmd.c_str(), cmd.length());
                        
                        SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());
                    }
                }
            }
            
            // 换行并更新 lastOutputEnd
            SendMessageW(hWnd, EM_SETSEL, -1, -1);
            SendMessageW(hWnd, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
            state->lastOutputEnd = GetWindowTextLengthW(hWnd);
            return 0;

        } else if (wParam == VK_UP || wParam == VK_DOWN) {
            if (state->history.empty()) return 0;
            
            // 历史记录导航
            if (wParam == VK_UP) {
                if (state->historyIndex == -1) {
                    int len = GetWindowTextLengthW(hWnd);
                    std::vector<wchar_t> buf(len + 1);
                    GetWindowTextW(hWnd, buf.data(), len + 1);
                    state->currentInput = &buf[state->lastOutputEnd];
                    state->historyIndex = (int)state->history.size() - 1;
                } else if (state->historyIndex > 0) {
                    state->historyIndex--;
                }
            } else { // VK_DOWN
                if (state->historyIndex != -1) {
                    state->historyIndex++;
                    if (state->historyIndex >= (int)state->history.size()) {
                        state->historyIndex = -1;
                    }
                }
            }

            // 替换当前输入行为历史记录
            std::wstring nextText = (state->historyIndex == -1) ? state->currentInput : state->history[state->historyIndex];
            SendMessageW(hWnd, EM_SETSEL, state->lastOutputEnd, -1);
            SendMessageW(hWnd, EM_REPLACESEL, TRUE, (LPARAM)nextText.c_str());
            return 0;

        } else if (wParam == VK_BACK) {
            if (start <= state->lastOutputEnd) return 0;
        } else if (wParam == VK_HOME) {
            SendMessageW(hWnd, EM_SETSEL, state->lastOutputEnd, state->lastOutputEnd);
            return 0;
        }
        break;
    }
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK TerminalDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        auto state = std::make_shared<TerminalState>();
        state->clientId = clientId;
        s_dlgStates[hDlg] = state;

        HWND hEditOut = GetDlgItem(hDlg, IDC_EDIT_TERM_OUT);
        SetWindowSubclass(hEditOut, TerminalOutEditSubclassProc, 0, (DWORD_PTR)hDlg);
        
        // 隐藏旧的输入框和发送按钮
        ShowWindow(GetDlgItem(hDlg, IDC_EDIT_TERM_IN), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_BTN_TERM_SEND), SW_HIDE);

        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_TERMINAL)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_TERMINAL)));

        if (g_hTermFont == NULL) {
            g_hTermFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            if (!g_hTermFont) {
                g_hTermFont = (HFONT)GetStockObject(ANSI_FIXED_FONT);
            }
        }
        
        if (g_hTermEditBkBrush == NULL) {
            g_hTermEditBkBrush = CreateSolidBrush(RGB(0, 0, 0));
        }

        SendDlgItemMessage(hDlg, IDC_EDIT_TERM_OUT, WM_SETFONT, (WPARAM)g_hTermFont, TRUE);
        SendDlgItemMessage(hDlg, IDC_EDIT_TERM_OUT, EM_SETLIMITTEXT, 0, 0);
        
        // 确保输出窗口可以接受焦点并输入
        SendMessageW(hEditOut, EM_SETREADONLY, FALSE, 0);

        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (client) {
            wchar_t szTitle[256];
            swprintf_s(szTitle, L"远程终端 - [%S]", client->ip.c_str());
            SetWindowTextW(hDlg, szTitle);
        }

        SetFocus(hEditOut);

        // 初始化常用命令列表
        HWND hList = GetDlgItem(hDlg, IDC_LIST_COMMON_CMDS);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        
        LVCOLUMNW lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.pszText = (LPWSTR)L"命令";
        lvc.cx = 150;
        ListView_InsertColumn(hList, 0, &lvc);
        
        lvc.pszText = (LPWSTR)L"说明";
        lvc.cx = 350;
        ListView_InsertColumn(hList, 1, &lvc);

        struct { const wchar_t* cmd; const wchar_t* desc; } commonCmds[] = {
            { L"systeminfo", L"查看系统详细信息" },
            { L"ipconfig /all", L"查看网络配置详情" },
            { L"netstat -ano", L"查看网络连接和端口占用" },
            { L"tasklist", L"列出当前运行的进程" },
            { L"whoami", L"查看当前用户信息" },
            { L"net user", L"列出系统本地用户" },
            { L"net localgroup administrators", L"查看管理员组成员" },
            { L"query user", L"查看当前登录会话" },
            { L"dir /s /b *.txt", L"递归搜索当前目录下的文本文件" },
            { L"sc query", L"查看系统服务状态" },
            { L"taskkill /f /im explorer.exe", L"强制结束资源管理器" },
            { L"start explorer.exe", L"启动资源管理器" }
        };

        for (int i = 0; i < _countof(commonCmds); i++) {
            LVITEMW lvi = { 0 };
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            lvi.pszText = (LPWSTR)commonCmds[i].cmd;
            ListView_InsertItem(hList, &lvi);
            ListView_SetItemText(hList, i, 1, (LPWSTR)commonCmds[i].desc);
        }

        ApplyModernTheme(hDlg);
        return (INT_PTR)FALSE;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == GetDlgItem(hDlg, IDC_EDIT_TERM_OUT)) {
            SetTextColor(hdc, RGB(0, 255, 0)); // 绿色文字
            SetBkColor(hdc, RGB(0, 0, 0));    // 黑色背景
            return (INT_PTR)g_hTermEditBkBrush;
        }
        break;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        // 调整各控件位置
        // Output (Terminal) - 占据上方大部分空间
        int margin = 8;
        int listHeight = 150;
        int outputHeight = height - listHeight - margin * 3;
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_TERM_OUT), margin, margin, width - margin * 2, outputHeight, TRUE);
        
        // Common Commands List - 占据下方空间
        int listY = margin + outputHeight + margin;
        MoveWindow(GetDlgItem(hDlg, IDC_LIST_COMMON_CMDS), margin, listY, width - margin * 2, listHeight, TRUE);
        
        return (INT_PTR)TRUE;
    }
    case WM_TERMINAL_APPEND_TEXT: {
        HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_TERM_OUT);
        LPCWSTR lpszText = (LPCWSTR)lParam;
        if (!lpszText) return TRUE;

        int len = GetWindowTextLengthW(hEdit);
        SendMessageW(hEdit, EM_SETSEL, len, len);
        SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)lpszText);
        SendMessageW(hEdit, WM_VSCROLL, SB_BOTTOM, 0);

        // 更新受保护区域结束位置
        auto it = s_dlgStates.find(hDlg);
        if (it != s_dlgStates.end()) {
            it->second->lastOutputEnd = GetWindowTextLengthW(hEdit);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_NOTIFY: {
        LPNMHDR nmhdr = (LPNMHDR)lParam;
        if (nmhdr->idFrom == IDC_LIST_COMMON_CMDS && nmhdr->code == NM_DBLCLK) {
            LPNMITEMACTIVATE nmia = (LPNMITEMACTIVATE)lParam;
            if (nmia->iItem != -1) {
                wchar_t szCmd[256];
                ListView_GetItemText(nmhdr->hwndFrom, nmia->iItem, 0, szCmd, 256);
                
                HWND hEditOut = GetDlgItem(hDlg, IDC_EDIT_TERM_OUT);
                auto it = s_dlgStates.find(hDlg);
                if (it != s_dlgStates.end()) {
                    auto state = it->second;
                    // 将命令追加到输出窗口并发送
                    SendMessageW(hEditOut, EM_SETSEL, state->lastOutputEnd, -1);
                    SendMessageW(hEditOut, EM_REPLACESEL, TRUE, (LPARAM)szCmd);
                    
                    // 模拟按下回车发送
                    SendMessageW(hEditOut, WM_KEYDOWN, VK_RETURN, 0);
                }
            }
        }
        break;
    }
    case WM_CLOSE:
        if (s_dlgStates.count(hDlg)) {
            uint32_t clientId = s_dlgStates[hDlg]->clientId;
            
            // 发送关闭终端命令给客户端，确保结束远程进程
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) client = g_Clients[clientId];
            }
            if (client) {
                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = Formidable::CMD_TERMINAL_CLOSE;
                
                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> sendBuf(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* h = (Formidable::PkgHeader*)sendBuf.data();
                memcpy(h->flag, "FRMD26?", 7);
                h->originLen = (int)bodySize;
                h->totalLen = (int)sendBuf.size();
                memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());

                client->hTerminalDlg = NULL;
            }
            s_dlgStates.erase(hDlg);
        }
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
}
    return (INT_PTR)FALSE;
}

HWND TerminalDialog::Show(HWND hParent, uint32_t clientId) {
    return CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_TERMINAL), hParent, DlgProc, (LPARAM)clientId);
}

} // namespace UI
} // namespace Formidable
