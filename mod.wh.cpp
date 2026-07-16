// ==WindhawkMod==
// @id            pivotlink-browser-router
// @name          PivotLink: Browser Router (GlazeWM)
// @description   Routes links to already running browser. Modified by Gemini to fix GlazeWM workspace switching by targeting the main HWND instead of blind window titles. Original: https://github.com/gauthumj/pivotlink-browser-router
// @version       1.5
// @author        gauthumj (modified)
// @include       *
// @compilerOptions -lshell32
// @license       MIT
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- browsers: "zen.exe, msedge.exe"
  $name: Priority Browsers
  $description: Comma-separated list of browser executables to route links to, in order of priority.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <windhawk_utils.h>
#include <mutex>
#include <vector>
#include <string>

std::mutex g_settingsMutex;
std::vector<std::wstring> g_priorityBrowsers;
thread_local bool t_inHook = false; 

// Function pointers
using ShellExecuteExW_t = decltype(&ShellExecuteExW);
ShellExecuteExW_t ShellExecuteExW_Original;

using ShellExecuteW_t = decltype(&ShellExecuteW);
ShellExecuteW_t ShellExecuteW_Original;

using ShellExecuteExA_t = decltype(&ShellExecuteExA);
ShellExecuteExA_t ShellExecuteExA_Original;

using ShellExecuteA_t = decltype(&ShellExecuteA);
ShellExecuteA_t ShellExecuteA_Original;

using CreateProcessW_t = decltype(&CreateProcessW);
CreateProcessW_t CreateProcessW_Original;

// Used to return both the process executable name and its main window handle
struct BrowserInfo {
    std::wstring exeName;
    HWND hwnd;
};

struct WindowCheck {
    DWORD processId;
    HWND mainHwnd;
};

// Callback to find the main browser window belonging to a specific Process ID
static BOOL CALLBACK CheckAnyWindowProc(HWND hwnd, LPARAM lParam) {
    WindowCheck* check = reinterpret_cast<WindowCheck*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    
    if (pid == check->processId) {
        WCHAR className[256];
        if (GetClassNameW(hwnd, className, 256)) {
            // Zen (Firefox based) uses MozillaWindowClass
            // Edge (Chromium based) uses Chrome_WidgetWin_1
            if (wcscmp(className, L"MozillaWindowClass") == 0 || 
                wcscmp(className, L"Chrome_WidgetWin_1") == 0) {
                check->mainHwnd = hwnd;
                return FALSE; // Window found, stop enumerating
            }
        }
    }
    return TRUE;
}

// Retrieves the main HWND for a given Process ID
static HWND GetMainWindowForPid(DWORD processId) {
    WindowCheck check = { processId, NULL };
    EnumWindows(CheckAnyWindowProc, reinterpret_cast<LPARAM>(&check));
    return check.mainHwnd;
}

BrowserInfo GetHighestPriorityRunningBrowser() {
    std::vector<std::wstring> browsers;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        browsers = g_priorityBrowsers;
    }

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return { L"", NULL };

    PROCESSENTRY32W pe = { sizeof(pe) };
    std::vector<std::vector<DWORD>> browserPids(browsers.size());

    if (Process32FirstW(hSnap, &pe)) {
        do {
            for (size_t i = 0; i < browsers.size(); ++i) {
                if (_wcsicmp(pe.szExeFile, browsers[i].c_str()) == 0) {
                    browserPids[i].push_back(pe.th32ProcessID);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    for (size_t i = 0; i < browsers.size(); ++i) {
        for (DWORD pid : browserPids[i]) {
            HWND hwnd = GetMainWindowForPid(pid);
            if (hwnd != NULL) {
                Wh_Log(L"Routing to: %s", browsers[i].c_str());
                return { browsers[i], hwnd };
            }
        }
    }
    return { L"", NULL };
}

const std::wstring& GetCurrentProcessName() {
    static std::wstring name = []() -> std::wstring {
        WCHAR path[MAX_PATH] = {};
        if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
            std::wstring sPath(path);
            size_t pos = sPath.find_last_of(L"\\/");
            if (pos != std::wstring::npos) return sPath.substr(pos + 1);
            return sPath;
        }
        return L"UNKNOWN";
    }();
    return name;
}

void LoadSettings() {
    // Default fallback if settings fail to load
    std::wstring browsersStr = L"zen.exe, msedge.exe";
    
    // Retrieve setting from Windhawk UI
    PCWSTR rawBrowser = Wh_GetStringSetting(L"browsers");
    if (rawBrowser) {
        browsersStr = rawBrowser;
        Wh_FreeStringSetting(rawBrowser);
    }
    
    std::vector<std::wstring> browsers;
    size_t start = 0;
    
    // Parse comma-separated list
    while (start < browsersStr.length()) {
        size_t end = browsersStr.find(L',', start);
        if (end == std::wstring::npos) {
            end = browsersStr.length();
        }
        
        std::wstring token = browsersStr.substr(start, end - start);
        
        // Trim leading/trailing whitespace
        size_t first = token.find_first_not_of(L" \t");
        if (first != std::wstring::npos) {
            size_t last = token.find_last_not_of(L" \t");
            browsers.push_back(token.substr(first, (last - first + 1)));
        }
        start = end + 1;
    }

    // Safely update the global priority list
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_priorityBrowsers = std::move(browsers);
}

std::wstring WideFromAnsi(LPCSTR str) {
    if (!str) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
    if (len <= 0) return L"";
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, str, -1, &out[0], len);
    return out;
}

bool RouteLinkIfNecessary(const WCHAR* lpFile, const WCHAR* lpVerb, const WCHAR* lpParameters, int nShow) {
    if (!lpFile || t_inHook) return false;

    if (lpVerb && _wcsicmp(lpVerb, L"open") != 0) return false;

    std::wstring cleanUrl = lpFile;
    if (cleanUrl.length() >= 2 && cleanUrl.front() == L'"' && cleanUrl.back() == L'"') {
        cleanUrl = cleanUrl.substr(1, cleanUrl.length() - 2);
    }

    bool isLink = (_wcsnicmp(cleanUrl.c_str(), L"http://", 7) == 0 || _wcsnicmp(cleanUrl.c_str(), L"https://", 8) == 0);
    if (!isLink) return false;

    Wh_Log(L"Link detected: %s", cleanUrl.c_str());

    std::wstring currentProc = GetCurrentProcessName();
    BrowserInfo target = GetHighestPriorityRunningBrowser();

    if (target.exeName.empty() || _wcsicmp(currentProc.c_str(), target.exeName.c_str()) == 0) {
        return false;
    }

    Wh_Log(L"Found target: %s - attempting to activate existing window", target.exeName.c_str());

    // Try to activate existing window first
    if (target.hwnd) {
        Wh_Log(L"Activating existing window");
        
        // These calls signal GlazeWM to swap to the correct workspace
        ShowWindow(target.hwnd, SW_RESTORE);
        SetForegroundWindow(target.hwnd);
        SwitchToThisWindow(target.hwnd, TRUE);
    } else {
        Wh_Log(L"Could not find existing window to activate");
    }

    // Now send the URL
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpFile = target.exeName.c_str();
    sei.lpParameters = cleanUrl.c_str();
    sei.nShow = SW_SHOW;

    t_inHook = true;
    BOOL success = ShellExecuteExW_Original(&sei);
    t_inHook = false;

    Wh_Log(L"URL send result: %s", success ? L"Success" : L"Failed");
    return success;
}

// Hook functions
BOOL WINAPI ShellExecuteExW_Hook(LPSHELLEXECUTEINFOW pExecInfo) {
    if (pExecInfo && pExecInfo->lpFile) {
        if (RouteLinkIfNecessary(pExecInfo->lpFile, pExecInfo->lpVerb, pExecInfo->lpParameters, pExecInfo->nShow)) {
            pExecInfo->hInstApp = (HINSTANCE)33;
            if (pExecInfo->fMask & SEE_MASK_NOCLOSEPROCESS)
                pExecInfo->hProcess = NULL;
            return TRUE;
        }
    }
    return ShellExecuteExW_Original(pExecInfo);
}

HINSTANCE WINAPI ShellExecuteW_Hook(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShow) {
    if (RouteLinkIfNecessary(lpFile, lpOperation, lpParameters, nShow)) {
        return (HINSTANCE)33;
    }
    return ShellExecuteW_Original(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShow);
}

BOOL WINAPI ShellExecuteExA_Hook(LPSHELLEXECUTEINFOA pExecInfo) {
    if (pExecInfo && pExecInfo->lpFile) {
        std::wstring file = WideFromAnsi(pExecInfo->lpFile);
        std::wstring verb = WideFromAnsi(pExecInfo->lpVerb);
        std::wstring params = WideFromAnsi(pExecInfo->lpParameters);
        if (RouteLinkIfNecessary(file.c_str(), pExecInfo->lpVerb ? verb.c_str() : NULL, pExecInfo->lpParameters ? params.c_str() : NULL, pExecInfo->nShow)) {
            pExecInfo->hInstApp = (HINSTANCE)33;
            if (pExecInfo->fMask & SEE_MASK_NOCLOSEPROCESS)
                pExecInfo->hProcess = NULL;
            return TRUE;
        }
    }
    return ShellExecuteExA_Original(pExecInfo);
}

HINSTANCE WINAPI ShellExecuteA_Hook(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShow) {
    std::wstring file = WideFromAnsi(lpFile);
    std::wstring op = WideFromAnsi(lpOperation);
    std::wstring params = WideFromAnsi(lpParameters);
    if (RouteLinkIfNecessary(file.c_str(), lpOperation ? op.c_str() : NULL, lpParameters ? params.c_str() : NULL, nShow)) {
        return (HINSTANCE)33;
    }
    return ShellExecuteA_Original(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShow);
}

BOOL WINAPI CreateProcessW_Hook(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment,
    LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation)
{
    return CreateProcessW_Original(lpApplicationName, lpCommandLine,
        lpProcessAttributes, lpThreadAttributes, bInheritHandles,
        dwCreationFlags, lpEnvironment, lpCurrentDirectory,
        lpStartupInfo, lpProcessInformation);
}

BOOL Wh_ModInit() {
    DWORD dwSessionId = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &dwSessionId) && dwSessionId == 0) {
        return FALSE; 
    }

    LoadSettings();

    WindhawkUtils::SetFunctionHook(ShellExecuteExW, ShellExecuteExW_Hook, &ShellExecuteExW_Original);
    WindhawkUtils::SetFunctionHook(ShellExecuteW, ShellExecuteW_Hook, &ShellExecuteW_Original);
    WindhawkUtils::SetFunctionHook(ShellExecuteExA, ShellExecuteExA_Hook, &ShellExecuteExA_Original);
    WindhawkUtils::SetFunctionHook(ShellExecuteA, ShellExecuteA_Hook, &ShellExecuteA_Original);
    WindhawkUtils::SetFunctionHook(CreateProcessW, CreateProcessW_Hook, &CreateProcessW_Original);

    return TRUE;
}

void Wh_ModUninit() {
}

void Wh_ModSettingsChanged() {
    LoadSettings();
}
