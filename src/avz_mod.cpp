#include <avz_mod.h>
#include <libavz.h>

ModInfo modInfo;

namespace {
struct ModCallbackGuard {
    ModCallbackGuard() { ++__aScriptManager.modRunDepth; }
    ~ModCallbackGuard() {
        --__aScriptManager.modRunDepth;
        __aScriptManager.MaintainMod();
    }
};
}

// 初始化、退出、脚本运行
extern "C" __declspec(dllexport) void __cdecl ModInit(HMODULE hinstDLL) {
    __aig.hInstance = hinstDLL;
}
extern "C" __declspec(dllexport) void __cdecl ModFinalize() {
    if (modInfo.IamMod) {
        ModRequestStop();
        __aScriptManager.MaintainMod();
        return;
    }
    __aScriptManager.willBeExit = true;
    for (int i = 0; !__aScriptManager.isExit && i < 50; ++i) {
        Sleep(20);
    }
}
extern "C" __declspec(dllexport) void __cdecl ModRunTotal() {
    __aScriptManager.RunTotal();
    __aScriptManager.MaintainMod();
}

extern "C" __declspec(dllexport) void __cdecl ModManage(int enabled, int user) {
    __aScriptManager.ManageMod(enabled != 0, user);
}
extern "C" __declspec(dllexport) void __cdecl ModRequestStop() {
    __aScriptManager.modStopRequested = true;
}
extern "C" __declspec(dllexport) int __cdecl ModGetLifecycleState() {
    return __aScriptManager.ModLifecycleState();
}

// 同步游戏状态
extern "C" __declspec(dllexport) void __cdecl SyncControllerState(int sync, char * state) {
    ModCallbackGuard guard;
    if (sync) {
        __aGameControllor.isAdvancedPaused = *(int *)(state);
        __aGameControllor.isUpdateWindow = *(int *)(state + 8);
    } else {
        *(int *)(state) = __aGameControllor.isAdvancedPaused;
        *(int *)(state + 4) = __aScriptManager.ModBusinessAllowed() && __aGameControllor.isSkipTick();
        *(int *)(state + 8) = __aGameControllor.isUpdateWindow;
    }
}
extern "C" __declspec(dllexport) int __cdecl BlockTimeArrived() {
    return __aScriptManager.blockDepth != 0 && ANowTime(__aScriptManager.blockTime.wave) == __aScriptManager.blockTime.time;
}

// 渲染
namespace avzmod {
    static double lastCallTime = 0.0;
    static double lastFinishTime = 0.0;
};
extern "C" __declspec(dllexport) void __cdecl BeforeDrawEveryTick() {
    ModCallbackGuard guard;
    // Hot loading can happen after logic dispatch but before this frame's drawing.
    // GlobalInit belongs to the first logic tick; drawing must wait for it.
    if (!__aig.tickManagers || !__aScriptManager.ModBusinessAllowed()) return;
    avzmod::lastCallTime = __AProfiler::CurrentTime();

    if (!__ABasicPainter::IsOpen3dAcceleration()) return;

    // 如果要改动这段代码请咨询零度
    if (__AD3dInfo::device != nullptr && __aGameControllor.isUpdateWindow) {
        __AD3dInfo::device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000, 0.0f, 0L);
    }
}
extern "C" __declspec(dllexport) void __cdecl DrawEveryTick() {
    ModCallbackGuard guard;
    if (!__aig.tickManagers || !__aScriptManager.ModBusinessAllowed()) return;
    __ABasicPainter::DrawEveryTick();
}
extern "C" __declspec(dllexport) void __cdecl AfterDrawEveryTick() {
    ModCallbackGuard guard;
    if (!__aig.tickManagers || !__aScriptManager.ModBusinessAllowed()) return;
    avzmod::lastFinishTime = __AProfiler::CurrentTime();
    __aProfiler.paintTime.push_back(avzmod::lastFinishTime - avzmod::lastCallTime);
}
