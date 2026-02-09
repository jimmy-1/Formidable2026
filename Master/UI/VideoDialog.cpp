#include "VideoDialog.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include "../resource.h"
#include "../GlobalState.h"
#include <map>
#include <mutex>
#include <vector>

extern std::map<uint32_t, std::shared_ptr<Formidable::ConnectedClient>> g_Clients;
extern std::mutex g_ClientsMutex;
extern bool SendDataToClient(std::shared_ptr<Formidable::ConnectedClient> client, const void* pData, int iLength);

namespace Formidable {
namespace UI {

static std::map<HWND, uint32_t> s_dlgToClientId;

static void SendCmd(uint32_t clientId, uint32_t cmd, uint32_t arg1 = 0, uint32_t arg2 = 0) {
    std::shared_ptr<ConnectedClient> client;
    {
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) client = g_Clients[clientId];
    }
    if (!client) return;

    size_t bodySize = sizeof(CommandPkg);
    std::vector<char> buffer(sizeof(PkgHeader) + bodySize);
    PkgHeader* h = (PkgHeader*)buffer.data();
    memcpy(h->flag, "FRMD26?", 7);
    h->originLen = (int)bodySize;
    h->totalLen = (int)buffer.size();

    CommandPkg* p = (CommandPkg*)(buffer.data() + sizeof(PkgHeader));
    p->cmd = (CommandType)cmd;
    p->arg1 = arg1;
    p->arg2 = arg2;

    SendDataToClient(client, buffer.data(), (int)buffer.size());
}

INT_PTR CALLBACK VideoDialog::DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        uint32_t clientId = (uint32_t)lParam;
        s_dlgToClientId[hDlg] = clientId;

        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_VIDEO)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_VIDEO)));
        
        ApplyModernTheme(hDlg);
        // 自动开始
        SendCmd(clientId, CMD_VIDEO_STREAM, 1);
        return (INT_PTR)TRUE;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(hDlg, &rc);
        // 视频预览区域
        HWND hStatic = GetDlgItem(hDlg, IDC_STATIC_VIDEO);
        if (hStatic) {
            MoveWindow(hStatic, 0, 0, rc.right, rc.bottom - 40, TRUE);
        }
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND: {
        uint32_t clientId = s_dlgToClientId[hDlg];
        switch (LOWORD(wParam)) {
        case IDC_BTN_VIDEO_START:
            SendCmd(clientId, CMD_VIDEO_STREAM, 1);
            break;
        case IDC_BTN_VIDEO_STOP:
            SendCmd(clientId, CMD_VIDEO_STREAM, 0);
            break;
        case IDCANCEL:
            PostMessage(hDlg, WM_CLOSE, 0, 0);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE: {
        uint32_t clientId = s_dlgToClientId[hDlg];
        // 停止视频流
        SendCmd(clientId, CMD_VIDEO_STREAM, 0);
        
        std::lock_guard<std::mutex> lock(g_ClientsMutex);
        if (g_Clients.count(clientId)) {
            g_Clients[clientId]->hVideoDlg = NULL;
        }
        s_dlgToClientId.erase(hDlg);
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
    }
    }
    return (INT_PTR)FALSE;
}

void VideoDialog::Show(HWND hParent, uint32_t clientId) {
    CreateDialogParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_VIDEO), hParent, DlgProc, (LPARAM)clientId);
}

} // namespace UI
} // namespace Formidable
