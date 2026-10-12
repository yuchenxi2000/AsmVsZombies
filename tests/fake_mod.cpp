#include <windows.h>
#include <avz_mod_lifecycle.h>

static int state = AMOD_IDLE, runs = 0, manages = 0, draws = 0;
[[maybe_unused]] static bool stopping = false;
static bool busy = false;
static void (__cdecl* callback)() = nullptr;
#define API extern "C" __declspec(dllexport)
API void __cdecl ModInit(HMODULE) {}
#ifndef MISSING_API
API void __cdecl ModFinalize() {}
#endif
API void __cdecl ModRunTotal() {
    ++runs;
    state = AMOD_SESSION;
    if (callback) callback();
}
API void __cdecl SyncControllerState(int sync, char* data) {
    if (!sync) reinterpret_cast<int*>(data)[1] = 0;
}
API int __cdecl BlockTimeArrived() { return 0; }
API void __cdecl BeforeDrawEveryTick() { ++draws; }
API void __cdecl DrawEveryTick() { ++draws; }
API void __cdecl AfterDrawEveryTick() { ++draws; }
#ifndef LEGACY_API
API void __cdecl ModManage(int, int) {
    ++manages;
    if (stopping && !busy) state = AMOD_STOPPED;
}
API void __cdecl ModRequestStop() { stopping = true; }
API int __cdecl ModGetLifecycleState() { return state; }
#endif
API void __cdecl TestBusy(int value) { busy = value != 0; }
API void __cdecl TestCallback(void (__cdecl* value)()) { callback = value; }
API int __cdecl TestRuns() { return runs; }
API int __cdecl TestManages() { return manages; }
API int __cdecl TestDraws() { return draws; }
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
