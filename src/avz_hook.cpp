#include "libavz.h"
#include <iostream>

extern "C" __declspec(dllexport) void __cdecl __AScriptHook() {
    __aScriptManager.ScriptHook();
}

void __AInstallHook() {
    DWORD temp;
    // 54BACD 地址 call 了这个虚函数
    VirtualProtect((void*)0x400000, 0x35E000, PAGE_EXECUTE_READWRITE, &temp);
    *(uint32_t*)0x667bc0 = (uint32_t)&__AScriptHook;
}

void __AUninstallHook() {
    DWORD temp;
    VirtualProtect((void*)0x400000, 0x35E000, PAGE_EXECUTE_READWRITE, &temp);
    *(uint32_t*)0x667bc0 = 0x452650;
}

extern "C" __declspec(dllexport) void __cdecl ModInit(HMODULE hinstDLL) {
    __aig.hInstance = hinstDLL;
}
extern "C" __declspec(dllexport) void __cdecl ModFinalize() {
    __aScriptManager.willBeExit = true;
    for (int i = 0; !__aScriptManager.isExit && i < 50; ++i) {
        Sleep(20);
    }
}
typedef void (__cdecl *FuncRegisterType)(HMODULE);
bool IamMod = true;
HMODULE hLoader = 0;
// TMD，为什么MinGW必须要有这个，要不然链接libavzmod.a得到的dll就没有export symbol？
// 我写了__declspec(dllexport)都没用，一定要有个DllMain才能export，什么玩意儿，浪费我好多时间调试
BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        // attach to process
        // return FALSE to fail DLL load
        // test if we are loaded as mod
        if (*(uint32_t*)0x667bc0 != 0x452650) {
            // the hook is on, I am a mod
            IamMod = true;
            // init
            ModInit(hinstDLL);
            // register me!
            hLoader = GetModuleHandleW(L"avzloader.dll");
            if (!hLoader) {
                std::cerr << "Failed to find avzloader.dll!" << std::endl;
                break;
            }
            FuncRegisterType registerFunc = (FuncRegisterType)GetProcAddress(hLoader, "RegisterMod");
            if (!registerFunc) {
                std::cerr << "Failed to register mod!" << std::endl;
                break;
            }
            registerFunc(hinstDLL);
        } else {
            IamMod = false;
            ModInit(hinstDLL);
            __AInstallHook();
        }
        break;

    case DLL_PROCESS_DETACH:
        // detach from process
        if (IamMod) {
            // finalize
            ModFinalize();
            // unregister me!
            if (!hLoader) {
                break;
            }
            FuncRegisterType unregisterFunc = (FuncRegisterType)GetProcAddress(hLoader, "UnregisterMod");
            if (!unregisterFunc) {
                std::cerr << "Failed to unregister mod!" << std::endl;
                break;
            }
            unregisterFunc(hinstDLL);
        } else {
            // if we are not loaded as mod, finalize and uninstall hook
            ModFinalize();
            __AUninstallHook();
        }
        break;

    case DLL_THREAD_ATTACH:
        // attach to thread
        break;

    case DLL_THREAD_DETACH:
        // detach from thread
        break;
    }
    return TRUE; // succesful
}
