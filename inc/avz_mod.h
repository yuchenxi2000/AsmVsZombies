#ifndef __AVZ_MOD_H__
#define __AVZ_MOD_H__

#include <windows.h>
#include <iostream>

typedef void (__cdecl *FuncRegisterType)(HMODULE);

class ModInfo {
public:
    bool IamMod;
    HMODULE hLoader;
    HMODULE hMod;
    FuncRegisterType registerFunc;
    FuncRegisterType unregisterFunc;

    ModInfo() {
        IamMod = false;
        hLoader = 0;
        hMod = 0;
        registerFunc = nullptr;
        unregisterFunc = nullptr;
    }

    bool InitLoaderAPI() {
        registerFunc = (FuncRegisterType)GetProcAddress(hLoader, "RegisterMod");
        if (!registerFunc) {
            return false;
        }
        unregisterFunc = (FuncRegisterType)GetProcAddress(hLoader, "UnregisterMod");
        if (!unregisterFunc) {
            return false;
        }
        return true;
    }

    bool InitAsMod(HMODULE hMod) {
        IamMod = true;
        hLoader = GetModuleHandleW(L"avzloader.dll");
        if (!hLoader) {
            std::cerr << "Failed to find avzloader.dll!" << std::endl;
            return false;
        }
        if (!InitLoaderAPI()) {
            std::cout << "Failed to register loader API!" << std::endl;
            return false;
        }
        // register me!
        registerFunc(hMod);
        return true;
    }

    bool FinalizeAsMod() {
        if (!hLoader) {
            return false;
        }
        if (!unregisterFunc) {
            std::cerr << "Failed to unregister mod!" << std::endl;
            return false;
        }
        unregisterFunc(hMod);
        return true;
    }
};

extern ModInfo modInfo;

extern "C" __declspec(dllexport) void __cdecl ModInit(HMODULE hinstDLL);
extern "C" __declspec(dllexport) void __cdecl ModFinalize();
extern "C" __declspec(dllexport) void __cdecl SyncControllerState(int sync, char * state);
extern "C" __declspec(dllexport) void __cdecl ModRunTotal();
extern "C" __declspec(dllexport) int __cdecl BlockTimeArrived();
extern "C" __declspec(dllexport) void __cdecl BeforeDrawEveryTick();
extern "C" __declspec(dllexport) void __cdecl DrawEveryTick();
extern "C" __declspec(dllexport) void __cdecl AfterDrawEveryTick();

#endif
