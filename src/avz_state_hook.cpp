#include "libavz.h"

#define __ADefineHookClassFuncs(HookName, BusinessHook)                                      \
    __APublic##HookName##Hook::__APublic##HookName##Hook(int runOrder)                       \
    {                                                                                        \
        __ARegisterInitOp([this] {                                                           \
            this->_isRunAll = false;                                                         \
            this->_isRun = false;                                                            \
        });                                                                                  \
        _iter = _GetHookContainer().emplace(runOrder, this);                                 \
    }                                                                                        \
    __APublic##HookName##Hook::~__APublic##HookName##Hook()                                  \
    {                                                                                        \
        _GetHookContainer().erase(_iter);                                                    \
    }                                                                                        \
    void __APublic##HookName##Hook::Run##HookName()                                          \
    {                                                                                        \
        if (!_isRun) {                                                                       \
            _##HookName();                                                                   \
            _isRun = true;                                                                   \
        }                                                                                    \
    }                                                                                        \
    void __APublic##HookName##Hook::Reset()                                                  \
    {                                                                                        \
        _isRunAll = false;                                                                   \
        for (auto&& hook : _GetHookContainer()) {                                            \
            hook.second->_isRun = false;                                                     \
        }                                                                                    \
    }                                                                                        \
    void __APublic##HookName##Hook::RunAll()                                                 \
    {                                                                                        \
        if (!_isRunAll) {                                                                    \
            for (auto&& hook : _GetHookContainer()) {                                        \
                if (BusinessHook && !__aScriptManager.ModBusinessAllowed())                  \
                    return;                                                                  \
                hook.second->Run##HookName();                                                \
            }                                                                                \
            _isRunAll = true;                                                                \
        }                                                                                    \
    }                                                                                        \
    __APublic##HookName##Hook::HookContainer& __APublic##HookName##Hook::_GetHookContainer() \
    {                                                                                        \
        static HookContainer _;                                                              \
        return _;                                                                            \
    }                                                                                        \
    bool __APublic##HookName##Hook::_isRunAll = false

__ADefineHookClassFuncs(BeforeScript, true);
__ADefineHookClassFuncs(AfterScript, true);
__ADefineHookClassFuncs(EnterFight, true);
__ADefineHookClassFuncs(ExitFight, false);
// Injection and shutdown hooks complete their once-per-DLL work as a group.
__ADefineHookClassFuncs(AfterInject, false);
__ADefineHookClassFuncs(BeforeTick, true);
__ADefineHookClassFuncs(AfterTick, true);
__ADefineHookClassFuncs(BeforeExit, false);
