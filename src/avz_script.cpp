#include "libavz.h"
#include "avz_mod.h"

namespace {
// Raised only in AvZ C++ code after a game call has returned, never through game assembly.
struct ModWaitCancelled {};

struct BlockDepthGuard {
    int& depth;
    bool active = true;
    BlockDepthGuard(int& depth) : depth(depth) { ++depth; }
    void Release() {
        if (active) {
            --depth;
            active = false;
        }
    }
    ~BlockDepthGuard() { Release(); }
};

struct ModRunGuard {
    __AScriptManager& manager;
    bool hosted;
    ModRunGuard(__AScriptManager& manager) : manager(manager), hosted(modInfo.IamMod) {
        if (hosted)
            ++manager.modRunDepth;
    }
    ~ModRunGuard() {
        if (hosted)
            --manager.modRunDepth;
    }
};
}

__AModContext __AScriptManager::ModContext() const {
    auto base = AGetPvzBase();
    auto board = base->MainObject();
    return {reinterpret_cast<uintptr_t>(board), base->GameUi(), base->LevelId(), modUser,
        board ? board->GameClock() : 0};
}

void __AScriptManager::ManageMod(bool enabled, int user) {
    modEnabled = enabled;
    modUser = user;
    modSession.Observe(ModContext());
    MaintainMod();
}

void __AScriptManager::EndModSession() {
    if (!modSession.active)
        return;
    __APublicExitFightHook::RunAll();
    // ExitFight destroys suspended coroutines; discard their callbacks before the next session.
    if (__aig.tickManagers) {
        for (int mode : {ATickRunner::GLOBAL, ATickRunner::ONLY_FIGHT, ATickRunner::PAINT})
            __aig.tickManagers[mode].Clear();
    }
    __aOpQueueManager.queues.clear();
    __aOpQueueManager.isInitialized = false;
    isLoaded = false;
    isBlockable = true;
    blockDepth = 0;
    modRunFlag = -1;
    isNeedEnterGame = isNeedBackToMain = false;
    continueCountdown = backToMainCountdown = enterGameCountdown = -1;
    modSession.End(static_cast<int>(scriptReloadMode), ModContext());
}

void __AScriptManager::MaintainMod() {
    if (!modInfo.IamMod || modRunDepth != 0 || modCleanupFailed || isExit)
        return;
    ModRunGuard runGuard(*this);
    try {
        modSession.Observe(ModContext());
        modSession.ObserveMain(ModContext());
        if (modSession.endPending || modStopRequested || willBeExit)
            EndModSession();
        if (modSession.finished)
            willBeExit = true;
        if (modStopRequested || willBeExit) {
            willBeExit = true;
            if (modAfterInjectDone && !modBeforeExitDone) {
                // Mark before invoking author code so reentry cannot repeat shutdown hooks.
                modBeforeExitDone = true;
                __APublicBeforeExitHook::RunAll();
            }
            // AfterInject callbacks may launch coroutines before AScript starts a session.
            __aCoHandleManager.Clear();
            __aOpQueueManager.queues.clear();
            __aOpQueueManager.isInitialized = false;
            if (__aig.tickManagers) {
                for (int mode = 0; mode < ATickRunner::__COUNT; ++mode)
                    __aig.tickManagers[mode].Clear();
            }
            isExit = true;
        }
    } catch (const std::exception& error) {
        modCleanupFailed = true;
        std::cerr << "Mod cleanup failed; keeping DLL loaded: " << error.what() << std::endl;
    } catch (...) {
        modCleanupFailed = true;
        std::cerr << "Mod cleanup failed; keeping DLL loaded" << std::endl;
    }
}

void __AScriptManager::CheckModWait() {
    if (!modInfo.IamMod)
        return;
    modSession.Observe(ModContext());
    if (modStopRequested || willBeExit || !modEnabled || modSession.endPending) {
        if (modInitializing)
            modSession.endPending = true;
        throw ModWaitCancelled {};
    }
}

int __AScriptManager::ModLifecycleState() const {
    int state = modSession.active ? AMOD_SESSION : AMOD_IDLE;
    if (modInitializing || modRunDepth != 0)
        state |= AMOD_INITIALIZING;
    if (modSession.waitMain)
        state |= AMOD_WAIT_MAIN;
    if (modSession.finished)
        state |= AMOD_FINISHED;
    if (isExit && modRunDepth == 0 && !modCleanupFailed)
        state |= AMOD_STOPPED;
    if (modCleanupFailed)
        state |= AMOD_FAILED;
    return state;
}

bool __AScriptManager::ModBusinessAllowed() {
    if (!modInfo.IamMod)
        return true;
    modSession.Observe(ModContext());
    return modEnabled && !modStopRequested && !willBeExit && !isExit && !modCleanupFailed
        && !modSession.endPending && !modSession.finished && !modSession.waitMain;
}

void __AScriptManager::GlobalInit() {
    static bool isInit = false;
    if (isInit)
        return;
    isInit = true;
    static ALogger<AMsgBox> logger;
    logger.SetLevel({ALogLevel::ERROR, ALogLevel::WARNING});
    aLogger = &logger;
    static __ATickManager tickManagers[ATickRunner::__COUNT];
    __aig.tickManagers = tickManagers;
    __aig.tickManagers[ATickRunner::AFTER_INJECT].SetRunMode(ATickRunner::AFTER_INJECT);
    __aig.tickManagers[ATickRunner::ONLY_FIGHT].SetRunMode(ATickRunner::ONLY_FIGHT);
    __aig.tickManagers[ATickRunner::GLOBAL].SetRunMode(ATickRunner::GLOBAL);
    __aig.tickManagers[ATickRunner::PAINT].SetRunMode(ATickRunner::PAINT);
}

bool __AScriptManager::MemoryInit() {
    int gameUi = AGetPvzBase()->GameUi();
    if (gameUi != 2 && gameUi != 3)
        return false;
    int gameIdx = AGetPvzBase()->MRef<int>(0x7f8); // 关卡序号
    if (gameIdx == AAsm::CHALLENGE_ZEN_GARDEN)     // 进入禅静花园直接返回
        return false;
    auto mainObject = AGetPvzBase()->MainObject();
    if (!mainObject)
        return false;

    // 假进入战斗界面直接返回
    if (mainObject->LevelEndCountdown())
        return false;

    for (auto&& initOp : __aig.GetInitOps())
        initOp();
    return true;
}

void __AScriptManager::LoadScript() {
    if (modInfo.IamMod && !modSession.CanBegin())
        return;
    if (!__AScriptManager::MemoryInit())
        return;
    if (modInfo.IamMod)
        modSession.Begin(ModContext());
    struct InitGuard {
        bool& initializing;
        InitGuard(bool& initializing) : initializing(initializing) { initializing = true; }
        ~InitGuard() { initializing = false; }
    } initGuard(modInitializing);
    isLoaded = true;
    __APublicBeforeScriptHook::RunAll();
    CheckModWait();
    __aOpQueueManager.UpdateRefreshTime(); // 刷新一次
    void AScript();
    AScript();
    CheckModWait();
    __APublicAfterScriptHook::RunAll();
    CheckModWait();

    RunTotal();

    if (modInfo.IamMod)
        return; // The host observes session completion without retaining this stack for a whole fight.

    // 等待游戏进入战斗界面
    while (AGetPvzBase()->GameUi() == 2 && AGetPvzBase()->MainObject())
        AAsm::GameSleepLoop();

    // 等待游戏结束
    while (AGetPvzBase()->GameUi() == 3 && AGetPvzBase()->MainObject())
        AAsm::GameSleepLoop();

    __APublicExitFightHook::RunAll();
    ASetAdvancedPause(false);

    if (scriptReloadMode != AReloadMode::MAIN_UI_OR_FIGHT_UI) {
        // 如果战斗界面不允许重新注入则等待回主界面
        while (AGetPvzBase()->MainObject())
            AAsm::GameSleepLoop();
    }

    // 当递归深度为 0 和 scriptReloadMode > 0 时, 才能重置 isLoaded
    isLoaded = !(int(scriptReloadMode) > 0 && blockDepth == 0);
    willBeExit = isLoaded;
}

void __AScriptManager::RunScript() {
    int gameUi = AGetPvzBase()->GameUi();

    // 如果在非战斗界面还检测到开启了不更新窗口
    // 那么直接强制开启更新窗口
    // 不然在 UpdateApp 函数中会检测当前游戏是否进行了刷新
    // 如果没有进行刷新就会一直死循环跳不出去
    if (gameUi != 3 && !__aGameControllor.isUpdateWindow) {
        ASetUpdateWindow(true);
    }

    // 如果在选卡界面或者在战斗界面开启了不刷新窗口
    // 则执行全局运行帧
    if (gameUi == 2
        || ((gameUi == 3) && !__aGameControllor.isUpdateWindow)) {
        __aig.tickManagers[ATickRunner::GLOBAL].RunQueue();
        return;
    }

    // 下面的代码只能在战斗界面运行
    if (gameUi != 3)
        return;

    // 开启了高级暂停之后
    // AvZ 战斗逻辑的代码均不能执行
    // 这里只会执行刷新高级暂停界面的函数
    if (__aGameControllor.isAdvancedPaused) {
        __aig.tickManagers[ATickRunner::GLOBAL].RunQueue();
        __aGameControllor.UpdateAdvancedPause();
        return;
    }

    auto mainObject = AGetMainObject();

    static int64_t standaloneRunFlag = -1;
    auto& runFlag = modInfo.IamMod ? modRunFlag : standaloneRunFlag;
    auto gameClock = mainObject->GameClock();
    if (runFlag == gameClock) { // 保证此函数下面的内容一帧只会运行一次
        // 但是全局运行帧依然可以运行
        __aig.tickManagers[ATickRunner::GLOBAL].RunQueue();
        return;
    }

    runFlag = gameClock;
    __APublicEnterFightHook::RunAll();
    CheckModWait();
    __aOpQueueManager.UpdateRefreshTime();

    isBlockable = false;
    __aOpQueueManager.RunOperation();
    CheckModWait();
    __aig.tickManagers[ATickRunner::GLOBAL].RunQueue();
    CheckModWait();
    __aig.tickManagers[ATickRunner::ONLY_FIGHT].RunQueue();
    isBlockable = true;
}

void __AScriptManager::RunTotal() {
    ModRunGuard runGuard(*this);
    if (!ModBusinessAllowed())
        return;
    constexpr auto stopWorkingStr = " || AvZ has stopped working !!!";
    static double lastCallTime, lastFinishTime;

    lastCallTime = __AProfiler::CurrentTime();
    if (!__aProfiler.avzTime.empty())
        __aProfiler.pvzTime.push_back(lastCallTime - lastFinishTime);

    try {
        if (willBeExit) {
            static bool isRunBeforeExitHook = false;
            if (!isRunBeforeExitHook) {
                __APublicBeforeExitHook::RunAll();
                isRunBeforeExitHook = true;
            }
            isExit = true;
            return;
        }

        // 这里有最基础的全局初始化，任何功能都必须在这之后运行
        // 而且这只进行一次初始化，对性能毫无影响
        GlobalInit();

        __APublicBeforeTickHook::RunAll();
        __APublicBeforeTickHook::Reset();
        CheckModWait();

        static bool isRunAfterInjectHook = false;
        if (!(modInfo.IamMod ? modAfterInjectDone : isRunAfterInjectHook)) {
            if (modInfo.IamMod)
                modAfterInjectDone = true;
            __APublicAfterInjectHook::RunAll();
            isRunAfterInjectHook = true;
        }
        CheckModWait();

        // 运行 AFTER_INJECT 运行帧
        __aig.tickManagers[ATickRunner::AFTER_INJECT].RunQueue();
        CheckModWait();

        FastSaveLoad();
        CheckModWait();

        if (isLoaded) // 运行脚本
            RunScript();
        else // 载入脚本
            LoadScript();

        CheckModWait();
        __APublicAfterTickHook::RunAll();
        __APublicAfterTickHook::Reset();
    } catch (const ModWaitCancelled&) {
        isBlockable = true;
        // A suspended synchronous initializer cannot resume after being filtered out.
        // End it at the outer boundary; do not destroy queues on a reentrant tick.
        if (modInitializing)
            modSession.endPending = true;
    } catch (AException& exce) {
        std::string exceMsg = exce.what();
        if (exceMsg != ASTR_GAME_RET_MAIN_UI) {
            willBeExit = true;
            exceMsg = "catch avz exception: " + exceMsg + stopWorkingStr;
            aLogger->Info(exceMsg.c_str());
        } else {
            if (modInfo.IamMod) {
                modSession.endPending = true;
            } else {
                // 当递归深度为 0 和 scriptReloadMode > 0 时, 才能重置 isLoaded
                isLoaded = !(int(scriptReloadMode) > 0 && blockDepth == 0);
                willBeExit = isLoaded;
                aLogger->Info(exceMsg.c_str());
                __APublicExitFightHook::RunAll();
            }
        }
    } catch (std::exception& exce) {
        AMsgBox::Show(std::string("catch std exception: ") + exce.what() + stopWorkingStr);
        willBeExit = true;
    } catch (...) {
        AMsgBox::Show(std::string("The script triggered an unknown exception. ") + stopWorkingStr);
        willBeExit = true;
    }

    lastFinishTime = __AProfiler::CurrentTime();
    __aProfiler.avzTime.push_back(lastFinishTime - lastCallTime);
}

void __AScriptManager::ScriptHook() {
    RunTotal();

    if (!__aGameControllor.isUpdateWindow)
        return;
    AAsm::GameTotalLoop();
    while (__aGameControllor.isSkipTick() && AGetPvzBase()->MainObject()) {
        RunTotal();
        if (__aGameControllor.isAdvancedPaused)
            return;
        if (AGameIsPaused()) // 防止游戏暂停时开启跳帧发生死锁
            return;
        if (blockDepth != 0 && ANowTime(blockTime.wave) == blockTime.time) {
            // 阻塞时间到达，必须通知阻塞函数释放阻塞
            return;
        }
        AGetPvzBase()->MjClock() += 1;
        AAsm::GameFightLoop();
        AAsm::ClearObjectMemory();
        AAsm::CheckFightExit();
    }
}

void __AScriptManager::WaitForFight(bool isSkipTick) {
    CheckModWait();
    if (AGetPvzBase()->GameUi() == 3)
        return;
    if (!isBlockable) {
        aLogger->Error("连接和帧运行内部不允许调用 WaitForFight");
        return;
    }

    if (blockDepth != 0) {
        aLogger->Error("请等待上一个阻塞函数时间到达之后再调用 WaitForFight");
        return;
    }
    BlockDepthGuard blockGuard(blockDepth);

    for (int cnt = 0; AGetPvzBase()->GameUi() == 2 && cnt < 2; ++cnt) {
        // 画面刷新几帧防止被系统杀死
        AAsm::GameSleepLoop();
        CheckModWait();
    }
    if (isSkipTick) {
        for (; AGetPvzBase()->GameUi() == 2;) {
            if (modInfo.IamMod) {
                modInfo.loaderRunFunc();
            } else {
                RunTotal();
            }
            CheckModWait();
            AAsm::UpdateFrame();
            CheckModWait();
        }
    }
    for (; AGetPvzBase()->GameUi() == 2;) {
        AAsm::GameSleepLoop();
        CheckModWait();
    }

    blockGuard.Release();
    if (!AGetPvzBase()->MainObject())
        AExitFight();
    __aOpQueueManager.UpdateRefreshTime(); // 刷新一次
    __APublicEnterFightHook::RunAll();
}

// avzloader不会支持，因为被标注为deprecated
void __AScriptManager::WaitUntil(int wave, int time) {
    if (!isBlockable) {
        aLogger->Error("连接和帧运行内部不允许调用 AWaitUntil");
        return;
    }

    if (blockDepth != 0) {
        aLogger->Error("请等待上一个阻塞函数时间到达之后再调用 AWaitUntil");
        return;
    }
    blockTime = {wave, time};
    WaitForFight(false);
    auto nowTime = ANowTime(wave);
    if (nowTime > time) {
        aLogger->Warning("现在的时间点已到 {}, 但是您要求的阻塞结束时间点为 {}, 此阻塞无意义",
            ATime(wave, nowTime), ATime(wave, time));
        return;
    }
    if (nowTime == time)
        return;
    BlockDepthGuard blockGuard(blockDepth);
    while (ANowTime(wave) < time) {
        AAsm::GameSleepLoop();
        CheckModWait();
        if (!AGetPvzBase()->MainObject()) {
            blockGuard.Release();
            AExitFight();
        }
        __aOpQueueManager.UpdateRefreshTime(); // 实时刷新当前时间
    }
}

void __AScriptManager::FastSaveLoad() {
    // 这里所有的 countdown 目标只有一个
    // 保证 EnterGame 点击对话框 DoBackToMain 不在同一帧运行
    // 留一定的缓冲时间归 AvZ 反应
    if (isNeedEnterGame && enterGameCountdown < 0) {
        AAsm::EnterGame(gameMode);
        isNeedEnterGame = false;
        continueCountdown = 3;
        backToMainCountdown = 5;
    }

    // 点掉继续对话框
    if (continueCountdown == 0 && !hasContinueDialog) {
        auto topWindow = AGetPvzBase()->MouseWindow()->TopWindow();
        if (topWindow)
            AAsm::MouseClick(280, 370, 1);
    }
    if (continueCountdown >= 0)
        --continueCountdown;
    if (backToMainCountdown >= 0)
        --backToMainCountdown;
    if (enterGameCountdown >= 0)
        --enterGameCountdown;

    if (isNeedBackToMain && backToMainCountdown < 0) {
        if (isSaveData)
            AAsm::SaveData();
        AAsm::DoBackToMain();
        isNeedBackToMain = false;
    }
}

void __AScriptManager::EnterGame(int gameMode, bool hasContinueDialog) {
    gameMode = std::clamp(gameMode, 0, 73);
    this->gameMode = gameMode;
    this->hasContinueDialog = hasContinueDialog;
    isNeedEnterGame = true;
}

void __AScriptManager::BackToMain(bool isSaveData) {
    isNeedBackToMain = true;
    this->isSaveData = isSaveData;
    enterGameCountdown = 5;
}
