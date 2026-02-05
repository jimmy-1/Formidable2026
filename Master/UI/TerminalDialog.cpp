// TerminalDialog.cpp - 终端对话框实现
#include "TerminalDialog.h"
#include "../resource.h"
#include "../StringUtils.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include "../Utils/StringHelper.h"
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
};

static std::map<HWND, std::shared_ptr<TerminalState>> s_dlgStates;

// 终端输入框子类化过程
LRESULT CALLBACK TerminalInEditSubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    HWND hDlg = (HWND)dwRefData;
    auto it = s_dlgStates.find(hDlg);
    if (it == s_dlgStates.end()) return DefSubclassProc(hWnd, message, wParam, lParam);
    auto state = it->second;

    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_UP) {
            if (state->history.empty()) return 0;
            if (state->historyIndex == -1) {
                // 保存当前输入的文本
                int len = GetWindowTextLengthW(hWnd);
                std::vector<wchar_t> buf(len + 1);
                GetWindowTextW(hWnd, buf.data(), len + 1);
                state->currentInput = buf.data();
                state->historyIndex = (int)state->history.size() - 1;
            } else if (state->historyIndex > 0) {
                state->historyIndex--;
            }
            SetWindowTextW(hWnd, state->history[state->historyIndex].c_str());
            SendMessageW(hWnd, EM_SETSEL, -1, -1); // 移动光标到末尾
            return 0;
        } else if (wParam == VK_DOWN) {
            if (state->historyIndex != -1) {
                state->historyIndex++;
                if (state->historyIndex >= (int)state->history.size()) {
                    state->historyIndex = -1;
                    SetWindowTextW(hWnd, state->currentInput.c_str());
                } else {
                    SetWindowTextW(hWnd, state->history[state->historyIndex].c_str());
                }
                SendMessageW(hWnd, EM_SETSEL, -1, -1);
            }
            return 0;
        } else if (wParam == VK_RETURN) {
            // 触发发送
            SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDC_BTN_TERM_SEND, BN_CLICKED), (LPARAM)GetDlgItem(hDlg, IDC_BTN_TERM_SEND));
            return 0;
        }
        break;
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

        HWND hEditIn = GetDlgItem(hDlg, IDC_EDIT_TERM_IN);
        SetWindowSubclass(hEditIn, TerminalInEditSubclassProc, 0, (DWORD_PTR)hDlg);

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
        SendDlgItemMessage(hDlg, IDC_EDIT_TERM_IN, WM_SETFONT, (WPARAM)g_hTermFont, TRUE);
        SendDlgItemMessage(hDlg, IDC_EDIT_TERM_OUT, EM_SETLIMITTEXT, 0, 0);

        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        if (client) {
            wchar_t szTitle[256];
            // Fix: ipAddress and info are not members of ConnectedClient, they should be ip and info->lanAddr
            // Wait, let's check ClientTypes.h again for member names
            swprintf_s(szTitle, L"远程终端 - [%S]", client->ip.c_str());
            SetWindowTextW(hDlg, szTitle);
            
            // 发送打开终端命令
            Formidable::CommandPkg pkg = { 0 };
            pkg.cmd = Formidable::CMD_TERMINAL_OPEN;
            
            size_t bodySize = sizeof(Formidable::CommandPkg);
            std::vector<char> sendBuf(sizeof(Formidable::PkgHeader) + bodySize);
            Formidable::PkgHeader* h = (Formidable::PkgHeader*)sendBuf.data();
            memcpy(h->flag, "FRMD26?", 7);
            h->originLen = (int)bodySize;
            h->totalLen = (int)sendBuf.size();
            memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
            
            SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());
        }

        SetFocus(GetDlgItem(hDlg, IDC_EDIT_TERM_IN));
        return (INT_PTR)FALSE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        int outputHeight = height - 60;
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_TERM_OUT), 5, 5, width - 10, outputHeight, TRUE);
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_TERM_IN), 5, outputHeight + 10, width - 10, 40, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_BTN_TERM_SEND || (LOWORD(wParam) == IDC_EDIT_TERM_IN && HIWORD(wParam) == EN_CHANGE)) {
            HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_TERM_IN);
            int len = GetWindowTextLengthW(hEdit);
            if (len <= 0) return (INT_PTR)TRUE;

            std::vector<wchar_t> buf(len + 1);
            GetWindowTextW(hEdit, buf.data(), len + 1);
            std::wstring wcmd = buf.data();

            // 如果是 EN_CHANGE 触发且没有换行符，则不处理（交给 IDC_BTN_TERM_SEND 或子类化的 ENTER 处理）
            if (LOWORD(wParam) == IDC_EDIT_TERM_IN && !wcsstr(buf.data(), L"\n")) {
                return (INT_PTR)TRUE;
            }

            // 清理换行符
            while (!wcmd.empty() && (wcmd.back() == L'\r' || wcmd.back() == L'\n')) wcmd.pop_back();
            if (wcmd.empty()) {
                SetWindowTextW(hEdit, L"");
                return (INT_PTR)TRUE;
            }

            auto it = s_dlgStates.find(hDlg);
            if (it != s_dlgStates.end()) {
                auto state = it->second;
                uint32_t clientId = state->clientId;

                // 添加到历史记录
                if (state->history.empty() || state->history.back() != wcmd) {
                    state->history.push_back(wcmd);
                    if (state->history.size() > 100) state->history.erase(state->history.begin());
                }
                state->historyIndex = -1;

                std::shared_ptr<Formidable::ConnectedClient> client;
                {
                    std::lock_guard<std::mutex> lock(g_ClientsMutex);
                    if (g_Clients.count(clientId)) client = g_Clients[clientId];
                }
                if (client) {
                    std::string cmd = Formidable::Utils::StringHelper::WideToUTF8(wcmd + L"\n");
                    Formidable::CommandPkg pkg = { 0 };
                    pkg.cmd = Formidable::CMD_TERMINAL_DATA;

                    size_t bodySize = sizeof(Formidable::CommandPkg) + cmd.length();
                    std::vector<char> sendBuf(sizeof(Formidable::PkgHeader) + bodySize);
                    Formidable::PkgHeader* h = (Formidable::PkgHeader*)sendBuf.data();
                    memcpy(h->flag, "FRMD26?", 7);
                    h->originLen = (int)bodySize;
                    h->totalLen = (int)sendBuf.size();
                    memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader), &pkg, sizeof(Formidable::CommandPkg));
                    memcpy(sendBuf.data() + sizeof(Formidable::PkgHeader) + sizeof(Formidable::CommandPkg), cmd.c_str(), cmd.length());

                    SendDataToClient(client, sendBuf.data(), (int)sendBuf.size());
                }
            }
            SetWindowTextW(hEdit, L"");
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        if (s_dlgStates.count(hDlg)) {
            uint32_t clientId = s_dlgStates[hDlg]->clientId;
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) {
                g_Clients[clientId]->hTerminalDlg = NULL;
            }
            s_dlgStates.erase(hDlg);
        }
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
}
    return (INT_PTR)FALSE;
}

void TerminalDialog::Show(HWND hParent, uint32_t clientId) {
    CreateDialogParamW(g_hInstance, MAKEINTRESOURCEW(IDD_TERMINAL), hParent, DlgProc, (LPARAM)clientId);
}

} // namespace UI
} // namespace Formidable
