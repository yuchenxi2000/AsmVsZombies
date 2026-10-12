#ifndef AVZ_LOADER_MOD_MANAGER_H
#define AVZ_LOADER_MOD_MANAGER_H

#include <windows.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <list>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <toml++/toml.hpp>
#include <avz_mod_lifecycle.h>

struct RunConf {
    std::vector<std::string> users;
    std::vector<int> levels;
    bool exclude_user = true;
    bool exclude_level = true;
    bool restrict_level = false;

    bool ShouldRun(const std::string& user, int ui, int level) const {
        if (restrict_level && ui != 2 && ui != 3)
            return false;
        bool hasUser = std::find(users.begin(), users.end(), user) != users.end();
        bool hasLevel = std::find(levels.begin(), levels.end(), level) != levels.end();
        return hasUser != exclude_user && hasLevel != exclude_level;
    }
};

template <class T>
void TomlGetValueOrList(toml::node_view<const toml::node> node, std::vector<T>& values) {
    if (!node)
        return;
    if (auto value = node.value<T>()) {
        values.push_back(*value);
    } else if (auto array = node.as_array()) {
        for (const auto& element : *array) {
            auto value = element.value<T>();
            if (!value)
                throw std::runtime_error("include/exclude array contains an invalid value");
            values.push_back(*value);
        }
    } else {
        throw std::runtime_error("include/exclude must be a value or an array");
    }
}

struct ModConf {
    std::vector<RunConf> run_conf_list;

    static ModConf Read(const std::wstring& path) {
        ModConf result;
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                return result;
            throw std::runtime_error("cannot read mod configuration");
        }
        const auto config = toml::parse_file(path);
        auto runs = config["run"];
        if (!runs)
            return result;
        auto array = runs.as_array();
        if (!array)
            throw std::runtime_error("run must be an array of tables");
        for (const auto& entry : *array) {
            if (!entry.is_table())
                throw std::runtime_error("run must contain tables");
            RunConf rule;
            for (auto name : {"user", "level"}) {
                auto section = entry.at_path(name);
                if (section && !section.is_table())
                    throw std::runtime_error("user/level must be a table");
                if (section["include"] && section["exclude"])
                    throw std::runtime_error("include and exclude cannot be used together");
            }
            rule.exclude_user = !entry.at_path("user.include");
            rule.exclude_level = !entry.at_path("level.include");
            rule.restrict_level = entry.at_path("level.include") || entry.at_path("level.exclude");
            TomlGetValueOrList(entry.at_path("user.include"), rule.users);
            TomlGetValueOrList(entry.at_path("user.exclude"), rule.users);
            TomlGetValueOrList(entry.at_path("level.include"), rule.levels);
            TomlGetValueOrList(entry.at_path("level.exclude"), rule.levels);
            result.run_conf_list.push_back(std::move(rule));
        }
        return result;
    }

    bool ShouldRun(const std::string& user, int ui, int level) const {
        return run_conf_list.empty() || std::any_of(run_conf_list.begin(), run_conf_list.end(),
            [&](const RunConf& rule) { return rule.ShouldRun(user, ui, level); });
    }
};

inline std::wstring GetConfFileName(const std::wstring& dllName) {
    return dllName.substr(0, dllName.find_last_of(L'.')) + L".toml";
}

using FuncType = void (__cdecl*)();
using FuncEntryType = void (__cdecl*)(HMODULE);
using FuncSyncType = void (__cdecl*)(int, char*);
using FuncRetIntType = int (__cdecl*)();
using FuncManageType = void (__cdecl*)(int, int);

struct ControllerState {
    std::array<int, 3> values {0, 0, 1};
    int& AdvPause() { return values[0]; }
    int& SkipTick() { return values[1]; }
    int& UpdateWnd() { return values[2]; }
    char* Data() { return reinterpret_cast<char*>(values.data()); }
};

struct Mod {
    std::wstring modName;
    HMODULE hMod;
    int regType; // 1: folder-owned, 0: injector-owned (never FreeLibrary here).
    FILETIME lastWriteTime {};
    bool enabled = true;
    bool pendingUnload = false;
    bool detached = false;
    bool stopSent = false;
    bool reported = false;
    bool apiFailed = false;
    int lifecycle = AMOD_IDLE;
    unsigned activeCalls = 0;
    ModConf conf;
    FuncEntryType funcModInit;
    FuncType funcModFinalize;
    FuncType funcModRunTotal;
    FuncSyncType funcSyncControllerState;
    FuncRetIntType funcBlockTimeArrived;
    FuncType funcBeforeDrawEveryTick;
    FuncType funcDrawEveryTick;
    FuncType funcAfterDrawEveryTick;
    FuncManageType funcModManage;
    FuncType funcModRequestStop;
    FuncRetIntType funcModGetLifecycleState;

    Mod(std::wstring name, HMODULE handle, int source, FILETIME time = {})
        : modName(std::move(name)), hMod(handle), regType(source), lastWriteTime(time) {
#define MOD_API(name, type) func##name = reinterpret_cast<type>(GetProcAddress(hMod, #name))
        MOD_API(ModInit, FuncEntryType);
        MOD_API(ModFinalize, FuncType);
        MOD_API(ModRunTotal, FuncType);
        MOD_API(SyncControllerState, FuncSyncType);
        MOD_API(BlockTimeArrived, FuncRetIntType);
        MOD_API(BeforeDrawEveryTick, FuncType);
        MOD_API(DrawEveryTick, FuncType);
        MOD_API(AfterDrawEveryTick, FuncType);
        MOD_API(ModManage, FuncManageType);
        MOD_API(ModRequestStop, FuncType);
        MOD_API(ModGetLifecycleState, FuncRetIntType);
#undef MOD_API
    }

    bool CheckFunc() const {
        return funcModInit && funcModFinalize && funcModRunTotal && funcSyncControllerState
            && funcBlockTimeArrived && funcBeforeDrawEveryTick && funcDrawEveryTick && funcAfterDrawEveryTick;
    }
    bool HasLifecycle() const { return funcModManage && funcModRequestStop && funcModGetLifecycleState; }
    bool Runnable() const {
        return enabled && !pendingUnload && !detached && !apiFailed
            && !(lifecycle & (AMOD_STOPPED | AMOD_FAILED | AMOD_FINISHED | AMOD_WAIT_MAIN));
    }
};

struct ModFile {
    std::wstring name;
    FILETIME writeTime;
};

// A successful empty snapshot is distinct from an inaccessible directory.
inline bool ReadModDirectory(const std::wstring& directory, std::vector<ModFile>& result) {
    result.clear();
    DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW((directory + L"\\*.dll").c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND;
    }
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            result.push_back({data.cFileName, data.ftLastWriteTime});
    } while (FindNextFileW(handle, &data));
    DWORD error = GetLastError();
    FindClose(handle);
    return error == ERROR_NO_MORE_FILES;
}

class ModManager {
public:
    std::list<Mod> mod_list; // Stable records while game calls reenter the loader.
    ControllerState controllerState;
    unsigned dispatchDepth = 0;
    Mod* currentMod = nullptr;
    HMODULE pauseOwner = nullptr;
    HMODULE windowOwner = nullptr;
    void (*applyPause)(int) = nullptr;
    std::wstring directory;
    std::vector<ModFile> desired;
    bool scanPending = false;

    struct DispatchGuard {
        ModManager& manager;
        Mod* mod;
        Mod* previous;
        DispatchGuard(ModManager& manager, Mod* mod = nullptr)
            : manager(manager), mod(mod), previous(manager.currentMod) {
            ++manager.dispatchDepth;
            if (mod) {
                ++mod->activeCalls;
                manager.currentMod = mod;
            }
        }
        ~DispatchGuard() {
            manager.currentMod = previous;
            if (mod)
                --mod->activeCalls;
            --manager.dispatchDepth;
        }
    };

    template <class F, class... Args>
    std::invoke_result_t<F, Args...> Call(Mod& mod, F function, Args... args) {
        DispatchGuard guard(*this, &mod);
        try {
            return function(args...);
        } catch (...) {
            mod.apiFailed = true;
            mod.enabled = false;
            mod.lifecycle |= AMOD_FAILED;
            std::wcerr << L"Mod API failed; retaining DLL: " << mod.modName << std::endl;
            if constexpr (!std::is_void_v<std::invoke_result_t<F, Args...>>)
                return {};
        }
    }

    void AddToRegList(HMODULE module) { Enqueue(module, true); }
    void AddToUnRegList(HMODULE module) { Enqueue(module, false); }

    void RegisterAllMods() {
        std::vector<std::pair<HMODULE, bool>> commands;
        {
            std::lock_guard lock(regMutex);
            commands.swap(regCommands);
        }
        // Never hold regMutex across Windows loader APIs: DllMain takes these locks in the other order.
        for (const auto& command : commands) {
            HMODULE module = command.first;
            bool add = command.second;
            auto found = std::find_if(mod_list.begin(), mod_list.end(),
                [&](const Mod& mod) { return mod.hMod == module; });
            if (!add) {
                if (found != mod_list.end()) {
                    found->detached = true;
                    found->enabled = false;
                }
            } else if (found == mod_list.end()) {
                if (dispatchDepth) {
                    Enqueue(module, true);
                    continue;
                }
                wchar_t path[MAX_PATH];
                if (!GetModuleFileNameW(module, path, MAX_PATH))
                    continue;
                std::wstring name(path);
                name = name.substr(name.find_last_of(L"\\/") + 1);
                Mod mod(name, module, 0);
                if (mod.CheckFunc()) {
                    mod_list.push_back(std::move(mod));
                    std::wcout << L"Register injected mod: " << name << std::endl;
                }
            }
        }
        if (!dispatchDepth) {
            for (auto it = mod_list.begin(); it != mod_list.end();) {
                if (it->detached) {
                    ReleaseControls(*it);
                    it = mod_list.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    bool LoadAllMods() {
        std::vector<ModFile> snapshot;
        if (!ReadModDirectory(directory, snapshot)) {
            std::wcerr << L"Cannot scan mods; retaining previous snapshot: " << directory << std::endl;
            return false;
        }
        desired = std::move(snapshot);
        scanPending = true;
        for (auto& mod : mod_list) {
            if (mod.regType != 1 || mod.detached)
                continue;
            auto file = FindDesired(mod.modName);
            if (file == desired.end() || CompareFileTime(&mod.lastWriteTime, &file->writeTime) != 0) {
                mod.pendingUnload = true;
            } else {
                try {
                    mod.conf = ModConf::Read(directory + L"\\" + GetConfFileName(mod.modName));
                } catch (const std::exception& error) {
                    std::wcerr << L"Keeping previous config for " << mod.modName << L": ";
                    std::cerr << error.what() << std::endl;
                }
            }
        }
        Reconcile();
        return true;
    }

    void Reconcile() {
        if (dispatchDepth)
            return;
        RegisterAllMods();
        bool unloaded = false;
        for (auto it = mod_list.begin(); it != mod_list.end();) {
            auto& mod = *it;
            if (mod.regType != 1 || !mod.pendingUnload) {
                ++it;
                continue;
            }
            if (mod.HasLifecycle() && (mod.lifecycle & AMOD_STOPPED)
                && !(mod.lifecycle & AMOD_FAILED) && !mod.apiFailed && !mod.activeCalls) {
                HMODULE module = mod.hMod;
                ReleaseControls(mod);
                std::wcout << L"Unload mod: " << mod.modName << std::endl;
                it = mod_list.erase(it);
                FreeLibrary(module); // All author code has returned and cleanup was acknowledged.
                unloaded = true;
            } else {
                if (!mod.reported) {
                    std::wcout << L"Unload pending: " << mod.modName
                        << (mod.HasLifecycle() ? L" (waiting for cleanup)" : L" (recompile with the lifecycle protocol)")
                        << std::endl;
                    mod.reported = true;
                }
                ++it;
            }
        }
        RegisterAllMods(); // Drain detach commands before Windows reuses a module handle.
        if (!scanPending && !unloaded)
            return;
        scanPending = false;
        auto snapshot = desired;
        DispatchGuard loading(*this);
        for (const auto& file : snapshot) {
            if (std::any_of(mod_list.begin(), mod_list.end(), [&](const Mod& mod) {
                    return mod.regType == 1 && mod.modName == file.name;
                }))
                continue;
            ModConf config;
            try {
                config = ModConf::Read(directory + L"\\" + GetConfFileName(file.name));
            } catch (const std::exception& error) {
                std::cerr << "Cannot load mod configuration: " << error.what() << std::endl;
                continue;
            }
            HMODULE module = LoadLibraryW((directory + L"\\" + file.name).c_str());
            if (!module) {
                std::wcerr << L"Cannot load " << file.name << L": " << GetLastError() << std::endl;
                continue;
            }
            Mod mod(file.name, module, 1, file.writeTime);
            if (!mod.CheckFunc()) {
                // No script tick has run; never call an API that failed validation.
                FreeLibrary(module);
                std::wcerr << L"Missing mod API: " << file.name << std::endl;
                continue;
            }
            mod.conf = std::move(config);
            mod_list.push_back(std::move(mod));
            auto& added = mod_list.back();
            Call(added, added.funcModInit, module);
            std::wcout << L"Load mod: " << file.name << std::endl;
        }
    }

    void Manage(const std::string& user, int userId, int ui, int level) {
        RegisterAllMods();
        for (auto& mod : mod_list) {
            if (mod.detached || mod.apiFailed)
                continue;
            bool previouslyEnabled = mod.enabled;
            mod.enabled = mod.conf.ShouldRun(user, ui, level);
            if (previouslyEnabled && !mod.enabled)
                ReleaseControls(mod);
            if (!mod.HasLifecycle())
                continue;
            if (mod.pendingUnload && !mod.stopSent) {
                mod.stopSent = true;
                Call(mod, mod.funcModRequestStop);
            }
            int previous = mod.lifecycle;
            Call(mod, mod.funcModManage, int(mod.enabled && !mod.pendingUnload), userId);
            mod.lifecycle = Call(mod, mod.funcModGetLifecycleState);
            if (((previous & AMOD_SESSION) && !(mod.lifecycle & AMOD_SESSION))
                || (mod.lifecycle & AMOD_STOPPED))
                ReleaseControls(mod);
        }
    }

    void callModRunTotal() {
        for (auto& mod : mod_list) {
            if (!mod.Runnable())
                continue;
            int previous = mod.lifecycle;
            Call(mod, mod.funcModRunTotal);
            if (mod.HasLifecycle()) {
                mod.lifecycle = Call(mod, mod.funcModGetLifecycleState);
                if (((previous & AMOD_SESSION) && !(mod.lifecycle & AMOD_SESSION))
                    || (mod.lifecycle & AMOD_STOPPED))
                    ReleaseControls(mod);
            }
        }
    }

    template <class F>
    void Draw(F Mod::*function) {
        for (auto& mod : mod_list)
            if (mod.Runnable())
                Call(mod, mod.*function);
    }
    void callBeforeDrawEveryTick() { Draw(&Mod::funcBeforeDrawEveryTick); }
    void callDrawEveryTick() { Draw(&Mod::funcDrawEveryTick); }
    void callAfterDrawEveryTick() { Draw(&Mod::funcAfterDrawEveryTick); }

    bool BlockTimeArrived() {
        for (auto& mod : mod_list)
            if (mod.Runnable() && Call(mod, mod.funcBlockTimeArrived))
                return true;
        return false;
    }

    void SyncControllerState() {
        controllerState.SkipTick() = 0;
        for (auto& mod : mod_list) {
            if (!mod.Runnable())
                continue;
            ControllerState state;
            Call(mod, mod.funcSyncControllerState, 0, state.Data());
            // Evaluate every predicate: completing a skip request may invoke its callback.
            controllerState.SkipTick() |= state.SkipTick();
        }
    }
    int& isUpdateWindow() { return controllerState.UpdateWnd(); }
    int& isSkipTick() { return controllerState.SkipTick(); }
    int& isAdvancedPaused() { return controllerState.AdvPause(); }

    void SetAdvancedPaused(int paused) {
        controllerState.AdvPause() = paused;
        pauseOwner = paused && currentMod ? currentMod->hMod : nullptr;
        Broadcast();
        if (applyPause)
            applyPause(paused);
    }
    void SetUpdateWindow(int update) {
        controllerState.UpdateWnd() = update;
        windowOwner = !update && currentMod ? currentMod->hMod : nullptr;
        Broadcast();
    }

private:
    std::mutex regMutex;
    std::vector<std::pair<HMODULE, bool>> regCommands;
    void Enqueue(HMODULE module, bool add) {
        std::lock_guard lock(regMutex);
        regCommands.emplace_back(module, add);
    }
    std::vector<ModFile>::iterator FindDesired(const std::wstring& name) {
        return std::find_if(desired.begin(), desired.end(), [&](const ModFile& file) { return file.name == name; });
    }
    void Broadcast() {
        for (auto& mod : mod_list)
            if (!mod.detached && !mod.apiFailed)
                Call(mod, mod.funcSyncControllerState, 1, controllerState.Data());
    }
    void ReleaseControls(Mod& mod) {
        bool changed = false;
        if (pauseOwner == mod.hMod) {
            changed = true;
            pauseOwner = nullptr;
            controllerState.AdvPause() = 0;
            if (applyPause)
                applyPause(0);
        }
        if (windowOwner == mod.hMod) {
            changed = true;
            windowOwner = nullptr;
            controllerState.UpdateWnd() = 1;
        }
        if (changed)
            Broadcast();
    }
};

#endif
