#include <windows.h>
#include <cstdio>
#include <cstdarg>

#include <QCoreApplication>
#include <QTimer>
#include <QMetaObject>

#include "bgpatch.h"

// ─── Robust logging ─────────────────────────────────────────────────
// Writes to <DLL directory>\bgpatch.log (guaranteed writable).

static WCHAR g_logPathW[MAX_PATH] = {};

static void logInit(HINSTANCE hinstDLL)
{
    GetModuleFileNameW(hinstDLL, g_logPathW, MAX_PATH);
    WCHAR *slash = wcsrchr(g_logPathW, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat(g_logPathW, L"bgpatch.log");
}

void logWrite(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len < 0) return;
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char ts[64];
    int tsLen = wsprintfA(ts, "[%02d:%02d:%02d.%03d] ",
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    HANDLE hFile = g_logPathW[0] ? CreateFileW(g_logPathW, FILE_APPEND_DATA,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(hFile, ts, tsLen, &w, nullptr);
        WriteFile(hFile, buf, len, &w, nullptr);
        WriteFile(hFile, "\r\n", 2, &w, nullptr);
        CloseHandle(hFile);
    }

    OutputDebugStringA(ts);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

// ─── Initialisation ─────────────────────────────────────────────────

static DWORD WINAPI setupThread(LPVOID)
{
    logWrite("setupThread: started, waiting for Qt");

    // Wait for QCoreApplication
    for (int i = 0; i < 40; i++) {
        if (QCoreApplication::instance()) {
            logWrite("setupThread: QCoreApplication found after %ds", i);
            break;
        }
        if (i == 39)
            logWrite("setupThread: still no QCoreApplication at %ds", i + 1);
        Sleep(1000);
    }

    auto *app = QCoreApplication::instance();
    if (!app) {
        logWrite("setupThread: ERROR - QCoreApplication never appeared");
        return 1;
    }

    // Schedule init on the main GUI thread
    bool ok = QMetaObject::invokeMethod(
        app,
        []() {
            logWrite("MainThread: initialising BackgroundManager");
            BackgroundManager::instance()->init();

            auto *reloadTimer = new QTimer(BackgroundManager::instance());
            QObject::connect(reloadTimer, &QTimer::timeout, []() {
                BackgroundManager::instance()->reloadSettings();
            });
            reloadTimer->start(3000);
            logWrite("MainThread: BackgroundManager init done");
        },
        Qt::QueuedConnection);

    logWrite("setupThread: invokeMethod returned %d", (int)ok);
    return 0;
}

// ─── DLL Entry Point ─────────────────────────────────────────────────

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        OutputDebugStringA("=== bgpatch.dll DllMain(PROCESS_ATTACH) ===\n");
        DisableThreadLibraryCalls(hinstDLL);
        logInit(hinstDLL);
        logWrite("DllMain: PROCESS_ATTACH, hinst=%p", (void*)hinstDLL);

        // Log the full DLL path for diagnostics
        wchar_t modPathW[MAX_PATH];
        GetModuleFileNameW(hinstDLL, modPathW, MAX_PATH);
        char modPathA[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, modPathW, -1, modPathA, MAX_PATH, nullptr, nullptr);
        logWrite("DllMain: DLL path = %s", modPathA);

        HANDLE hThread = CreateThread(nullptr, 0, setupThread, nullptr, 0, nullptr);
        if (hThread) {
            logWrite("DllMain: setupThread created, handle=%p", (void*)hThread);
            CloseHandle(hThread);
        } else {
            logWrite("DllMain: CreateThread FAILED, err=%lu", GetLastError());
        }
    }
    return TRUE;
}
