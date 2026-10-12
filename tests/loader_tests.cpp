#define CINTERFACE
#include "mod_manager.h"
#include "directory_monitor.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <d3d.h>

namespace fs = std::filesystem;
// Reserve data inside the test image at PvZ's pointer address, keeping the
// Windows process heap away from it. The array is never otherwise used.
[[gnu::used]] static unsigned char gamePadding[0x600000];
static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <class F> F Api(HMODULE module, const char* name) {
    auto result = reinterpret_cast<F>(GetProcAddress(module, name));
    Check(result != nullptr, "test API missing");
    return result;
}
static void Config(const fs::path& path, const char* text) { std::ofstream(path) << text; }
static LONG WINAPI TestFault(EXCEPTION_POINTERS* fault) {
    auto record = fault->ExceptionRecord;
    std::fprintf(stderr, "TEST native exception %08lX at %p, accessed %p\n",
        record->ExceptionCode, record->ExceptionAddress,
        reinterpret_cast<void*>(record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0));
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
static int renderCalls = 0;
static HRESULT WINAPI TestRenderState(IDirect3DDevice7*, D3DRENDERSTATETYPE, DWORD) {
    ++renderCalls;
    return S_OK;
}
static HRESULT WINAPI TestTextureStage(IDirect3DDevice7*, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) {
    ++renderCalls;
    return S_OK;
}
static HRESULT WINAPI TestClear(IDirect3DDevice7*, DWORD, D3DRECT*, DWORD, D3DCOLOR, D3DVALUE, DWORD) {
    ++renderCalls;
    return S_OK;
}

static void SessionTests() {
    __AModContext cards {100, 2, 13, 7, 0}, fight {100, 3, 13, 7, 20};
    __AModSession session;
    session.Begin(cards);
    session.Observe(fight);
    Check(session.active && !session.endPending, "cards -> fight must retain session");
    session.Observe(fight);
    Check(!session.endPending, "same session resume must retain state");
    session.Observe({100, 3, 13, 8, 21});
    Check(session.endPending, "user switch must end session");
    session.End(1, fight);
    Check(!session.CanBegin(), "MAIN_UI must wait for menu");
    session.ObserveMain({});
    Check(session.CanBegin(), "menu must release MAIN_UI wait");
    session.Begin(fight);
    session.Observe(cards);
    Check(session.endPending, "fight -> cards ends old round even with same board");
    session.End(2, cards);
    Check(session.CanBegin(), "fight reload mode must allow another round");
    session.Begin(fight);
    session.Observe({200, 3, 13, 7, 21});
    Check(session.endPending, "board replacement must end session");
    session.End(2, fight);
    session.Begin(fight);
    session.Observe({100, 3, 13, 7, 0});
    Check(session.endPending, "clock rollback must detect reused board");
    session.End(0, {});
    Check(!session.CanBegin() && session.finished, "NONE must never reload");
}

static ModManager* reentrantManager;
static fs::path reentrantFile;
static HMODULE reentrantModule;
static void __cdecl DuringRun() {
    fs::rename(reentrantFile, reentrantFile.wstring() + L".disabled");
    Check(reentrantManager->LoadAllMods(), "reentrant empty scan failed");
    reentrantManager->Manage("Ycx", 7, 3, 13);
    reentrantManager->Reconcile();
    Check(GetModuleHandleW(reentrantFile.filename().c_str()) == reentrantModule,
        "DLL must stay loaded until outer callback returns even after stop ack");
    Check(reentrantManager->mod_list.size() == 1, "record invalidated during callback");
}

static void ManagerTests(const fs::path& fixtures, const fs::path& directory) {
    ModManager manager;
    manager.directory = directory.wstring();
    auto script = directory / "script.dll";
    fs::copy_file(fixtures / "modern.dll", script);
    auto config = directory / "script.toml";
    Config(config, "[[run]]\n[run.user]\ninclude = 'Ycx'\n[run.level]\ninclude = [13, 14]\n");
    Check(manager.LoadAllMods() && manager.mod_list.size() == 1, "initial mod load failed");
    manager.directory = config.wstring();
    Check(!manager.LoadAllMods() && manager.mod_list.size() == 1 && manager.desired.size() == 1,
        "failed directory scan must preserve previous registry and snapshot");
    manager.directory = directory.wstring();
    auto module = manager.mod_list.front().hMod;
    auto runs = Api<int (__cdecl*)()>(module, "TestRuns");
    auto manages = Api<int (__cdecl*)()>(module, "TestManages");
    auto draws = Api<int (__cdecl*)()>(module, "TestDraws");
    manager.Manage("Ycx", 7, 3, 13);
    manager.callModRunTotal();
    Check(runs() == 1, "allowed script must run");
    {
        ModManager::DispatchGuard guard(manager, &manager.mod_list.front());
        manager.SetAdvancedPaused(1);
        manager.SetUpdateWindow(0);
    }
    manager.Manage("Other", 8, 3, 13);
    manager.callModRunTotal();
    manager.callDrawEveryTick();
    Check(runs() == 1 && draws() == 0 && manages() == 2, "disabled mod must manage but not dispatch");
    Check(!manager.isAdvancedPaused() && manager.isUpdateWindow(), "disabled owner must release controls");
    Config(config, "[[run]]\n[run.user]\ninclude = [123]\n");
    manager.LoadAllMods();
    manager.Manage("Other", 8, 3, 13);
    manager.callModRunTotal();
    Check(runs() == 1, "invalid config must retain last good restrictions");
    Config(config, "[[run]]\n[run.user]\ninclude = 'Ycx'\n[run.level]\ninclude = 13\n");
    manager.LoadAllMods();
    manager.Manage("Ycx", 7, 1, 13);
    manager.callModRunTotal();
    Check(runs() == 1, "level-restricted script must not run in menu");
    manager.Manage("Ycx", 7, 3, 13);
    reentrantManager = &manager;
    reentrantFile = script;
    reentrantModule = module;
    Api<void (__cdecl*)(void (__cdecl*)())>(module, "TestCallback")(&DuringRun);
    manager.callModRunTotal();
    manager.Reconcile();
    Check(manager.mod_list.empty() && !GetModuleHandleW(L"script.dll"), "safe point must unload stopped DLL");

    fs::copy_file(fixtures / "modern.dll", directory / "delayed.dll");
    manager.LoadAllMods();
    module = manager.mod_list.front().hMod;
    auto busy = Api<void (__cdecl*)(int)>(module, "TestBusy");
    busy(1);
    fs::rename(directory / "delayed.dll", directory / "delayed.disabled");
    manager.LoadAllMods();
    for (int i = 0; i < 100; ++i) {
        manager.Manage("Ycx", 7, 3, 13);
        manager.Reconcile();
    }
    Check(GetModuleHandleW(L"delayed.dll") == module, "no timeout may force unload busy DLL");
    busy(0);
    manager.Manage("Ycx", 7, 3, 13);
    manager.Reconcile();
    Check(manager.mod_list.empty(), "cleanup ack must finish delayed unload");

    module = LoadLibraryW((fixtures / "modern.dll").c_str());
    Check(module != nullptr, "external fixture load failed");
    manager.AddToRegList(module);
    manager.Manage("Ycx", 7, 3, 13);
    manager.LoadAllMods();
    Check(manager.mod_list.size() == 1 && manager.mod_list.front().regType == 0,
        "empty folder scan must preserve injected mod");
    manager.AddToUnRegList(module);
    manager.RegisterAllMods();
    Check(manager.mod_list.empty() && GetModuleHandleW(L"modern.dll") == module,
        "unregister must never free injector-owned DLL");
    FreeLibrary(module);

    fs::copy_file(fixtures / "legacy.dll", directory / "legacy.dll");
    manager.LoadAllMods();
    module = manager.mod_list.front().hMod;
    fs::rename(directory / "legacy.dll", directory / "legacy.disabled");
    manager.LoadAllMods();
    manager.Manage("Ycx", 7, 3, 13);
    manager.Reconcile();
    Check(manager.mod_list.size() == 1 && GetModuleHandleW(L"legacy.dll") == module,
        "legacy DLL without ack protocol must stay resident");
    manager.AddToUnRegList(module);
    manager.RegisterAllMods();
    FreeLibrary(module); // Test owns this final teardown, outside any callback.

    fs::copy_file(fixtures / "missing.dll", directory / "missing.dll");
    manager.LoadAllMods();
    Check(manager.mod_list.empty() && !GetModuleHandleW(L"missing.dll"),
        "invalid export set must be rejected without invoking missing finalize");
    fs::remove(directory / "missing.dll");
    std::vector<ModFile> snapshot;
    Check(!ReadModDirectory(config.wstring(), snapshot), "non-directory scan must fail");
}

static void MonitorTests(const fs::path& directory) {
    MonitorParam params;
    params.targetDir = directory.wstring();
    params.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, MonitorThreadProc, &params, 0, nullptr);
    Check(thread != nullptr && params.stopEvent, "watcher startup failed");
    // Retry writes until the worker has issued its first asynchronous read.
    unsigned generation = params.generation.load();
    for (int i = 0; i < 100 && params.generation.load() == generation; ++i) {
        Config(directory / "watch.toml", "value = 1\n");
        Sleep(10);
    }
    Check(params.generation.load() != generation, "watcher lost initial change");
    generation = params.generation.load();
    for (int i = 0; i < 100 && params.generation.load() == generation; ++i) {
        Config(directory / "watch.toml", "value = 2\n");
        Sleep(10);
    }
    Check(params.generation.load() != generation, "watcher must retain later change");
    SetEvent(params.stopEvent);
    Check(WaitForSingleObject(thread, 2000) == WAIT_OBJECT_0, "overlapped read cancellation hung");
    CloseHandle(thread);
    CloseHandle(params.stopEvent);
}

static void AvzCleanupTests(const fs::path& fixtures) {
    // No game assembly is executed; AvZ only reads one instruction word from
    // the mapped test image and the pointer below from reserved test data.
    Check(reinterpret_cast<uintptr_t>(gamePadding) <= 0x6a9ec0
        && reinterpret_cast<uintptr_t>(gamePadding) + sizeof(gamePadding) > 0x6a9ec4,
        "game pointer must fall inside reserved test data");
    std::vector<unsigned char> base(0x9000), board(0x6000), rounds(0x100), renderRoot(0x40), renderData(0x40);
    *reinterpret_cast<void**>(board.data() + 0x160) = rounds.data();
    *reinterpret_cast<void**>(0x6a9ec0) = base.data();
    *reinterpret_cast<HWND*>(base.data() + 0x350) = GetDesktopWindow();
    *reinterpret_cast<void**>(base.data() + 0x768) = board.data();
    *reinterpret_cast<int*>(base.data() + 0x7fc) = 3;
    *reinterpret_cast<int*>(base.data() + 0x7f8) = 13;
    std::cout << "Testing real AvZ cleanup" << std::endl;
    HMODULE module = LoadLibraryW((fixtures / "session.dll").c_str());
    Check(module != nullptr, "real AvZ session fixture load failed");
    SetUnhandledExceptionFilter(&TestFault);
    // Reproduce a freshly loaded mod receiving a 3D draw before its first logic tick.
    Check(Api<int (__cdecl*)()>(module, "TestDormantRunners")(),
        "default runners must be stopped before initialization");
    IDirect3DDevice7Vtbl deviceTable {};
    deviceTable.SetRenderState = &TestRenderState;
    deviceTable.SetTextureStageState = &TestTextureStage;
    deviceTable.Clear = &TestClear;
    IDirect3DDevice7 device {&deviceTable};
    *reinterpret_cast<void**>(base.data() + 0x36c) = renderRoot.data();
    *reinterpret_cast<void**>(renderRoot.data() + 0x30) = renderData.data();
    // Non-null sentinels suffice for interfaces not invoked while a z-buffer already exists.
    for (int offset : {0x10, 0x14, 0x18, 0x1c})
        *reinterpret_cast<void**>(renderData.data() + offset) = reinterpret_cast<void*>(1);
    *reinterpret_cast<void**>(renderData.data() + 0x20) = &device;
    Check(Api<int (__cdecl*)()>(module, "TestDrawBeforeInit")() && renderCalls == 0,
        "draw before first logic tick must skip painting without initializing or touching the device");
    *reinterpret_cast<void**>(base.data() + 0x36c) = nullptr;
    auto prepare = Api<void (__cdecl*)()>(module, "TestPrepare");
    auto manage = Api<void (__cdecl*)(int, int)>(module, "ModManage");
    auto status = Api<int (__cdecl*)()>(module, "ModGetLifecycleState");
    auto exits = Api<int (__cdecl*)()>(module, "TestExits");
    auto shutdowns = Api<int (__cdecl*)()>(module, "TestShutdowns");
    auto destroyed = Api<int (__cdecl*)()>(module, "TestCoroutineDestroyed");
    auto clean = Api<int (__cdecl*)()>(module, "TestClean");
    auto injectionAlive = Api<int (__cdecl*)()>(module, "TestInjectionAlive");
    prepare();
    Check(Api<int (__cdecl*)()>(module, "TestDormantRunners")() && !clean(),
        "default runners must not stop or alias running queue entries");
    *reinterpret_cast<void**>(base.data() + 0x36c) = renderRoot.data();
    Api<void (__cdecl*)()>(module, "DrawEveryTick")();
    Check(Api<int (__cdecl*)()>(module, "TestPaintRuns")() == 1 && renderCalls > 0,
        "initialized mod must still dispatch its paint queue normally");
    *reinterpret_cast<void**>(base.data() + 0x36c) = nullptr;
    Check(Api<int (__cdecl*)()>(module, "TestHookCancellation")(),
        "filter change inside a business hook must suppress the remaining hooks");
    manage(0, 7);
    int callsBeforeDisabledDraw = renderCalls;
    *reinterpret_cast<void**>(base.data() + 0x36c) = renderRoot.data();
    Api<void (__cdecl*)()>(module, "DrawEveryTick")();
    Check(Api<int (__cdecl*)()>(module, "TestPaintRuns")() == 1 && renderCalls == callsBeforeDisabledDraw,
        "disabled initialized mod must also skip drawing");
    *reinterpret_cast<void**>(base.data() + 0x36c) = nullptr;
    Check((status() & AMOD_SESSION) && exits() == 0 && shutdowns() == 0 && destroyed() == 0,
        "filter change alone must retain real AvZ session and coroutine");
    manage(1, 7);
    Check((status() & AMOD_SESSION) && !clean(), "same-session resume must retain queues");
    *reinterpret_cast<void**>(base.data() + 0x768) = nullptr;
    *reinterpret_cast<int*>(base.data() + 0x7fc) = 1;
    manage(0, 7);
    Check(!(status() & AMOD_SESSION) && exits() == 1 && shutdowns() == 0,
        "disabled session still must receive ExitFight exactly once");
    Check(clean() && destroyed() == 1 && injectionAlive(),
        "session end must destroy coroutine and clear session queues, preserving injection runner");
    manage(0, 7);
    Check(exits() == 1, "repeated maintenance must not duplicate ExitFight");
    *reinterpret_cast<void**>(base.data() + 0x768) = board.data();
    *reinterpret_cast<int*>(base.data() + 0x7fc) = 3;
    manage(1, 7);
    prepare();
    Api<void (__cdecl*)()>(module, "ModRequestStop")();
    manage(0, 7);
    Check((status() & AMOD_STOPPED) && exits() == 2 && shutdowns() == 1
        && destroyed() == 2 && clean() && !injectionAlive(),
        "shutdown ack requires all hooks, coroutines and runners to finish");
    manage(0, 7);
    Check(exits() == 2 && shutdowns() == 1, "shutdown must be idempotent");
    FreeLibrary(module);
    module = LoadLibraryW((fixtures / "session.dll").c_str());
    Check(module != nullptr, "cannot reload AvZ test fixture");
    SetUnhandledExceptionFilter(&TestFault);
    *reinterpret_cast<void**>(base.data() + 0x36c) = renderRoot.data();
    int callsBeforeReload = renderCalls;
    Check(Api<int (__cdecl*)()>(module, "TestDrawBeforeInit")() && renderCalls == callsBeforeReload,
        "a reloaded mod must again wait for its own first logic tick before drawing");
    *reinterpret_cast<void**>(base.data() + 0x36c) = nullptr;
    Api<void (__cdecl*)()>(module, "TestOutsideSession")();
    Api<void (__cdecl*)()>(module, "ModRequestStop")();
    Api<void (__cdecl*)(int, int)>(module, "ModManage")(0, 7);
    Check((Api<int (__cdecl*)()>(module, "ModGetLifecycleState")() & AMOD_STOPPED)
        && Api<int (__cdecl*)()>(module, "TestCoroutineDestroyed")() == 1
        && Api<int (__cdecl*)()>(module, "TestClean")(),
        "shutdown must clean coroutines and queues even without an active script session");
    FreeLibrary(module);
    *reinterpret_cast<void**>(0x6a9ec0) = nullptr;
}

static void AvzProcessExitTest(const fs::path& fixtures, bool initialized) {
    Check(reinterpret_cast<uintptr_t>(gamePadding) <= 0x6a9ec0
        && reinterpret_cast<uintptr_t>(gamePadding) + sizeof(gamePadding) > 0x6a9ec4,
        "game pointer must fall inside reserved test data");
    std::vector<unsigned char> base(0x9000), board(0x6000), rounds(0x100);
    *reinterpret_cast<void**>(0x6a9ec0) = base.data();
    *reinterpret_cast<void**>(board.data() + 0x160) = rounds.data();
    *reinterpret_cast<HWND*>(base.data() + 0x350) = GetDesktopWindow();
    *reinterpret_cast<void**>(base.data() + 0x768) = board.data();
    *reinterpret_cast<int*>(base.data() + 0x7fc) = 3;
    *reinterpret_cast<int*>(base.data() + 0x7f8) = 13;
    HMODULE module = LoadLibraryW((fixtures / "session.dll").c_str());
    Check(module != nullptr, "process exit fixture load failed");
    SetUnhandledExceptionFilter(&TestFault);
    Api<void (__cdecl*)()>(module, "TestRequireProcessExitBarrier")();
    if (initialized)
        Api<void (__cdecl*)()>(module, "TestPrepare")();
    // The game has destroyed its objects; DLLs remain resident with no stop
    // request. ExitProcess must run their actual CRT destructors safely.
    *reinterpret_cast<void**>(0x6a9ec0) = nullptr;
    std::cout << "Testing process exit with " << (initialized ? "active" : "uninitialized")
              << " AvZ DLL" << std::endl;
    ExitProcess(0);
}

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 2 && argc != 3) return 2;
    try {
        auto fixtures = fs::absolute(argv[1]);
        if (argc == 3) {
            std::string mode = argv[2];
            if (mode != "--exit-uninitialized" && mode != "--exit-active") return 2;
            AvzProcessExitTest(fixtures, mode == "--exit-active");
        }
        AvzCleanupTests(fixtures);
        auto directory = fixtures / ("run-" + std::to_string(GetCurrentProcessId()));
        fs::create_directories(directory);
        SessionTests();
        ManagerTests(fixtures, directory);
        MonitorTests(directory);
        fs::remove_all(directory);
        std::cout << "Loader lifecycle, DLL ownership, reentrant unload, config and watcher tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
