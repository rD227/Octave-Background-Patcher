#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <cstdio>
#include <cstdlib>

// ─── Configuration ───────────────────────────────────────────────────

struct Config {
    std::wstring imagePath;
    int opacity   = 30;   // 0-100
    int dimming   = 30;   // 0-100
    int scaleMode = 1;    // 0=fit, 1=fill, 2=stretch, 3=center, 4=tile
};

// ─── Helpers ─────────────────────────────────────────────────────────

static std::wstring GetExeDir()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    auto pos = s.rfind(L'\\');
    if (pos != std::wstring::npos) s.resize(pos);
    return s;
}

static bool FileExists(const std::wstring &path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring GetOctaveRoot(const std::wstring &exeDir)
{
    // exeDir = <octave_root>\home\octave-bg-patcher
    // Go up 2 levels to reach <octave_root>
    std::wstring dir = exeDir;
    for (int i = 0; i < 2; i++) {
        auto pos = dir.rfind(L'\\');
        if (pos == std::wstring::npos) break;
        dir.resize(pos);
    }
    // Verify by checking for octave.vbs marker file
    std::wstring marker = dir + L"\\octave.vbs";
    if (!FileExists(marker)) {
        // Try one more level up as fallback
        auto pos = dir.rfind(L'\\');
        if (pos != std::wstring::npos) dir.resize(pos);
    }
    return dir;
}

// ─── Config I/O ──────────────────────────────────────────────────────

static std::wstring toUnixPath(const std::wstring &s)
{
    std::wstring r = s;
    for (auto &c : r) if (c == L'\\') c = L'/';
    return r;
}

static std::wstring toWinPath(const std::wstring &s)
{
    std::wstring r = s;
    for (auto &c : r) if (c == L'/') c = L'\\';
    return r;
}

static void LoadConfig(const std::wstring &iniPath, Config &cfg)
{
    wchar_t buf[4096] = {};
    GetPrivateProfileStringW(L"Background", L"Image", L"", buf, 4096, iniPath.c_str());
    cfg.imagePath = toWinPath(buf);
    cfg.opacity   = GetPrivateProfileIntW(L"Background", L"Opacity",   30, iniPath.c_str());
    cfg.dimming   = GetPrivateProfileIntW(L"Background", L"Dimming",   30, iniPath.c_str());
    cfg.scaleMode = GetPrivateProfileIntW(L"Background", L"ScaleMode", 1,  iniPath.c_str());
}

static void SaveConfig(const std::wstring &iniPath, const Config &cfg)
{
    std::wstring imgPath = toUnixPath(cfg.imagePath);
    WritePrivateProfileStringW(L"Background", L"Image",     imgPath.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Background", L"Opacity",   std::to_wstring(cfg.opacity).c_str(),   iniPath.c_str());
    WritePrivateProfileStringW(L"Background", L"Dimming",   std::to_wstring(cfg.dimming).c_str(),   iniPath.c_str());
    WritePrivateProfileStringW(L"Background", L"ScaleMode", std::to_wstring(cfg.scaleMode).c_str(), iniPath.c_str());
}

// ─── DLL Injection ───────────────────────────────────────────────────

static bool InjectDLL(HANDLE hProcess, const std::wstring &dllPath)
{
    SIZE_T len = (dllPath.length() + 1) * sizeof(wchar_t);

    LPVOID remoteMem = VirtualAllocEx(hProcess, nullptr, len,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) return false;

    if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), len, nullptr)) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return false;
    }

    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    auto pLoadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(hK32, "LoadLibraryW");

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
                                         pLoadLib, remoteMem, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(hThread, 10000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);

    return exitCode != 0;
}

static bool LaunchOctave(const std::wstring &octaveRoot, const std::wstring &dllPath)
{
    std::wstring mingwBin = octaveRoot + L"\\mingw64\\bin";
    std::wstring qtBin    = octaveRoot + L"\\mingw64\\qt6\\bin";
    std::wstring usrBin   = octaveRoot + L"\\usr\\bin";
    std::wstring octaveExe = mingwBin + L"\\octave-gui.exe";

    if (!FileExists(octaveExe)) {
        MessageBoxW(nullptr, (L"octave-gui.exe not found:\n" + octaveExe).c_str(),
                    L"Error", MB_ICONERROR);
        return false;
    }

    // Build PATH
    wchar_t sysPath[32768] = {};
    GetEnvironmentVariableW(L"PATH", sysPath, 32768);
    std::wstring newPath = mingwBin + L";" + usrBin + L";" + qtBin + L";" + sysPath;

    // Build environment block (double-null-terminated)
    std::wstring env;
    auto addEnv = [&](const wchar_t *key, const wchar_t *val) {
        env += key; env += L"="; env += val; env.push_back(L'\0');
    };

    addEnv(L"PATH",               newPath.c_str());
    addEnv(L"MSYSTEM",            L"MINGW64");
    addEnv(L"TERM",               L"cygwin");
    addEnv(L"GNUTERM",            L"wxt");
    addEnv(L"GS",                 L"gs.exe");
    addEnv(L"QT_PLUGIN_PATH",     (mingwBin + L"\\..\\qt6\\plugins").c_str());
    addEnv(L"PKG_CONFIG_PATH",    (mingwBin + L"\\..\\lib\\pkgconfig").c_str());

    wchar_t homeBuf[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"HOME", homeBuf, MAX_PATH) == 0)
        GetEnvironmentVariableW(L"USERPROFILE", homeBuf, MAX_PATH);
    addEnv(L"HOME", homeBuf);
    addEnv(L"OPENBLAS_NUM_THREADS", L"4");

    env.push_back(L'\0'); // final null terminator

    // Create process suspended
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    std::wstring cmdLine = L"\"" + octaveExe + L"\" --gui";

    BOOL ok = CreateProcessW(octaveExe.c_str(), cmdLine.data(),
                              nullptr, nullptr, FALSE,
                              CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                              (LPVOID)env.data(), nullptr, &si, &pi);
    if (!ok) {
        MessageBoxW(nullptr, L"Failed to create Octave process.", L"Error", MB_ICONERROR);
        return false;
    }

    // Inject the DLL
    if (!InjectDLL(pi.hProcess, dllPath)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        MessageBoxW(nullptr, L"DLL injection failed.\nMake sure bgpatch.dll is in the same folder.",
                    L"Error", MB_ICONERROR);
        return false;
    }

    ResumeThread(pi.hThread);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return true;
}

// ─── WinMain ─────────────────────────────────────────────────────────

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    std::wstring exeDir    = GetExeDir();
    std::wstring dllPath   = exeDir + L"\\bgpatch.dll";
    std::wstring iniPath   = exeDir + L"\\bgpatch.ini";
    std::wstring octaveRoot = GetOctaveRoot(exeDir);

    Config cfg;
    if (FileExists(iniPath))
        LoadConfig(iniPath, cfg);

    // Check for /silent flag
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = false;
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (wcscmp(argv[i], L"/silent") == 0 || wcscmp(argv[i], L"-silent") == 0)
                silent = true;
        }
        LocalFree(argv);
    }

    if (silent) {
        // Direct launch without UI
        SaveConfig(iniPath, cfg);
        LaunchOctave(octaveRoot, dllPath);
        return 0;
    }

    // Check if dll exists
    if (!FileExists(dllPath)) {
        MessageBoxW(nullptr,
            L"bgpatch.dll not found in the patcher directory.\n\n"
            L"Please ensure the following files are in the same folder:\n"
            L"  - patcher.exe\n"
            L"  - bgpatch.dll\n"
            L"  - bgpatch.ini (will be created automatically)",
            L"Octave BG Patcher - Missing DLL", MB_ICONWARNING);
    }

    // Use a resource script for the settings dialog if we had one.
    // Instead, build the dialog at runtime from a memory template.
    // For simplicity, we'll show a basic settings flow:

    // Ask: configure first or launch directly?
    int choice = MessageBoxW(nullptr,
        L"Octave Background Image Patcher\n\n"
        L"Would you like to:\n"
        L"  [Yes]  Configure background image settings\n"
        L"  [No]   Launch Octave with current settings\n"
        L"  [Cancel]  Exit",
        L"Octave BG Patcher v1.0",
        MB_YESNOCANCEL | MB_ICONQUESTION);

    if (choice == IDCANCEL) return 0;

    if (choice == IDYES) {
        // Pick image file
        wchar_t fileBuf[MAX_PATH] = {};
        wcscpy(fileBuf, cfg.imagePath.c_str());

        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile   = fileBuf;
        ofn.nMaxFile    = MAX_PATH;
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
        ofn.lpstrTitle  = L"Select background image for Octave editor";

        if (GetOpenFileNameW(&ofn)) {
            cfg.imagePath = fileBuf;
        } else if (cfg.imagePath.empty()) {
            // No image selected and none configured
            MessageBoxW(nullptr, L"No image selected. Please run again to configure.",
                        L"Cancelled", MB_ICONINFORMATION);
            return 0;
        }
    }

    // Save config
    SaveConfig(iniPath, cfg);

    // Launch Octave
    if (choice != IDCANCEL) {
        if (LaunchOctave(octaveRoot, dllPath)) {
            MessageBoxW(nullptr,
                L"Octave launched with background image support!\n\n"
                L"To change the image, run patcher.exe again.\n"
                L"To launch without configuring, use: patcher.exe /silent\n\n"
                L"Edit bgpatch.ini to adjust opacity and dimming.",
                L"Octave BG Patcher",
                MB_OK | MB_ICONINFORMATION);
        }
    }

    return 0;
}
