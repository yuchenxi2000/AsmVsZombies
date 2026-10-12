#ifndef AVZ_DIRECTORY_MONITOR_H
#define AVZ_DIRECTORY_MONITOR_H

#include <windows.h>
#include <atomic>
#include <string>
#include <vector>

struct MonitorParam {
    std::wstring targetDir;
    HANDLE stopEvent = nullptr;
    std::atomic<unsigned> generation {1};
};

// The owner keeps MonitorParam alive for the loader's process lifetime.
inline DWORD WINAPI MonitorThreadProc(LPVOID parameter) {
    auto& params = *static_cast<MonitorParam*>(parameter);
    HANDLE directory = CreateFileW(params.targetDir.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory == INVALID_HANDLE_VALUE)
        return GetLastError();
    // ReadDirectoryChangesW requires a DWORD-aligned buffer.
    std::vector<DWORD> buffer(16384);
    OVERLAPPED request {};
    request.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!request.hEvent) {
        DWORD error = GetLastError();
        CloseHandle(directory);
        return error;
    }
    HANDLE waits[] {params.stopEvent, request.hEvent};
    while (WaitForSingleObject(params.stopEvent, 0) != WAIT_OBJECT_0) {
        ResetEvent(request.hEvent);
        if (!ReadDirectoryChangesW(directory, buffer.data(), DWORD(buffer.size() * sizeof(DWORD)),
                FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                nullptr, &request, nullptr))
            break;
        DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0 + 1) {
            CancelIoEx(directory, &request);
            DWORD bytes;
            GetOverlappedResult(directory, &request, &bytes, TRUE);
            break;
        }
        DWORD bytes;
        if (!GetOverlappedResult(directory, &request, &bytes, FALSE))
            break;
        ++params.generation; // Retained across cooldowns; no event can be consumed without a scan.
    }
    CloseHandle(request.hEvent);
    CloseHandle(directory);
    return 0;
}

#endif
