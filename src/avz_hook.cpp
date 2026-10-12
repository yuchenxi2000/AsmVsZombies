#include "libavz.h"
#include "avz_mod.h"

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

// TMD，为什么MinGW必须要有这个，要不然链接libavzmod.a得到的dll就没有export symbol？
// 我写了__declspec(dllexport)都没用，一定要有个DllMain才能export，什么玩意儿，浪费我好多时间调试
BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        // attach to process
        // return FALSE to fail DLL load
        // test if we are loaded as mod
        if (GetModuleHandleW(L"avzloader.dll") != nullptr) {
            // the hook is on, I am a mod
            // init
            ModInit(hinstDLL);
            // init as mod
            if (!modInfo.InitAsMod(hinstDLL))
                return FALSE;
        } else {
            modInfo.IamMod = false;
            ModInit(hinstDLL);
            __AInstallHook();
        }
        break;

    case DLL_PROCESS_DETACH:
        if (lpvReserved != nullptr) {
            // CRT global destructors still run after DllMain. Their runners
            // must not query queues/loggers that may already be destroyed.
            // Game objects and other threads may also be gone: no script hooks.
            __aScriptManager.willBeExit = true;
            __aScriptManager.isExit = true;
            __aig.tickManagers = nullptr;
            break;
        }
        // detach from process
        if (modInfo.IamMod) {
            // finalize
            ModFinalize();
            // finalize as mod
            modInfo.FinalizeAsMod();
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
