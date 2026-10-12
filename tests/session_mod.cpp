// Exercises the real AvZ cleanup code against data-only game memory.
#include <libavz.h>
#include <avz_mod.h>

static int exits = 0, shutdowns = 0, coroutineDestroyed = 0, paintRuns = 0;
static bool requireProcessExitBarrier = false;
struct ProcessExitProbe {
    ~ProcessExitProbe() {
        if (requireProcessExitBarrier && (!__aScriptManager.willBeExit
                || !__aScriptManager.isExit || __aig.tickManagers != nullptr
                || exits != 0 || shutdowns != 0))
            TerminateProcess(GetCurrentProcess(), 3);
    }
} static processExitProbe;
struct SilentLogger : AAbstractLogger {
    void _Output(ALogLevel level, std::string&& text) override {
        if (level == ALogLevel::ERROR || level == ALogLevel::WARNING)
            std::cerr << text;
    }
};
static ATickRunner globalRunner, fightRunner, paintRunner, injectionRunner;
static bool filterInHook = false;
static int laterHooks = 0;
struct EarlyTick : AOrderedBeforeTickHook<0> {
    void _BeforeTick() override {
        if (filterInHook) __aScriptManager.modEnabled = false;
    }
} static earlyTick;
struct LaterTick : AOrderedBeforeTickHook<1> {
    void _BeforeTick() override { ++laterHooks; }
} static laterTick;
struct Hooks : AStateHook {
    void _ExitFight() override { ++exits; }
    void _BeforeExit() override { ++shutdowns; }
} static hooks;
struct CoroutineLifetime {
    ~CoroutineLifetime() { ++coroutineDestroyed; }
};
static ACoroutine Suspended() {
    CoroutineLifetime lifetime;
    co_await [] { return false; };
}
void AScript() {}
static void __cdecl IgnoreState(int) {}
#define API extern "C" __declspec(dllexport)
API void __cdecl TestRequireProcessExitBarrier() { requireProcessExitBarrier = true; }
API int __cdecl TestDrawBeforeInit() {
    modInfo.IamMod = true;
    if (__aig.tickManagers != nullptr) return 0;
    BeforeDrawEveryTick();
    DrawEveryTick();
    AfterDrawEveryTick();
    __ABasicPainter::DrawEveryTick();
    return __aig.tickManagers == nullptr && __aProfiler.paintTime.empty()
        && !__aScriptManager.modAfterInjectDone;
}
API void __cdecl TestPrepare() {
    modInfo.IamMod = true;
    modInfo.advPauseFunc = modInfo.updateWndFunc = &IgnoreState;
    __aScriptManager.GlobalInit();
    static SilentLogger logger;
    ASetInternalLogger(logger); // Exercise logging while preventing modal dialogs in tests.
    __aScriptManager.modUser = 7;
    __aScriptManager.modAfterInjectDone = true;
    __aScriptManager.scriptReloadMode = AReloadMode::MAIN_UI_OR_FIGHT_UI;
    for (auto& operation : __aig.GetInitOps()) operation();
    __aScriptManager.modSession.Begin(__aScriptManager.ModContext());
    __aScriptManager.isLoaded = true;
    __aOpQueueManager.queues.resize(3);
    __aOpQueueManager.totalWave = 1;
    __aOpQueueManager.isInitialized = true;
    __aOpQueueManager.Push(ATime(1, 100), __ABoolOperation {[] {}});
    globalRunner.Start([] {}, ATickRunner::GLOBAL);
    fightRunner.Start([] {}, ATickRunner::ONLY_FIGHT);
    paintRunner.Start([] { ++paintRuns; }, ATickRunner::PAINT);
    if (injectionRunner.IsStopped()) injectionRunner.Start([] {}, ATickRunner::AFTER_INJECT);
    ACoLaunch(Suspended);
}
API int __cdecl TestExits() { return exits; }
API int __cdecl TestShutdowns() { return shutdowns; }
API int __cdecl TestCoroutineDestroyed() { return coroutineDestroyed; }
API int __cdecl TestClean() {
    return __aOpQueueManager.queues.empty() && !__aOpQueueManager.isInitialized
        && globalRunner.IsStopped() && fightRunner.IsStopped() && paintRunner.IsStopped();
}
API int __cdecl TestInjectionAlive() { return !injectionRunner.IsStopped(); }
API int __cdecl TestDormantRunners() {
    ATickHandle handle;
    ATickRunnerWithNoStart runner;
    // A default handle must never alias an existing queue entry, either
    // before GlobalInit or after other runners have started.
    bool stopped = handle.IsStopped() && runner.IsStopped() && aItemCollector.IsStopped();
    handle.Pause();
    runner.Stop();
    return stopped && handle.IsPaused();
}
API int __cdecl TestPaintRuns() { return paintRuns; }
API int __cdecl TestHookCancellation() {
    filterInHook = true;
    __APublicBeforeTickHook::Reset();
    __APublicBeforeTickHook::RunAll();
    filterInHook = false;
    return laterHooks == 0 && !__aScriptManager.modEnabled && __aScriptManager.modSession.active;
}
API void __cdecl TestOutsideSession() {
    TestPrepare();
    __aScriptManager.modSession.active = false;
}
// Exercise the production process-detach path without installing game hooks
// or registering with a loader during this data-only fixture's attachment.
#define DllMain AvzProcessDllMain
#include "../src/avz_hook.cpp"
#undef DllMain
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr)
        return AvzProcessDllMain(instance, reason, reserved);
    return TRUE;
}
