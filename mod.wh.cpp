// ==WindhawkMod==
// @id             pivotlink-browser-router
// @name           PivotLink: Browser Router
// @description    Lightweight link redirection tool with an intuitive 5-tier ranked configuration layout and gaming exclusions.
// @version        0.6
// @author         You
// @github         https://github.com/yourusername/smart-link-router
// @include        *
// @compilerOptions -lshell32
// @license        MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
High-performance, resource-aware layout designed to safely route sandboxed and native OS web links without degrading system or gaming performance.
Now features a structured, multi-tier ranking system for easier browser priority configuration.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- browser1: "brave.exe"
  $name: Priority 1 Browser (Highest)
  $description: Your primary choice for link redirection (e.g., brave.exe). Leave blank to skip.
- browser2: "firefox.exe"
  $name: Priority 2 Browser
  $description: Second choice browser if Priority 1 is not running. Leave blank to skip.
- browser3: "chrome.exe"
  $name: Priority 3 Browser
  $description: Third choice browser if higher priorities are closed. Leave blank to skip.
- browser4: "msedge.exe"
  $name: Priority 4 Browser
  $description: Fourth choice browser fallback. Leave blank to skip.
- browser5: ""
  $name: Priority 5 Browser (Lowest)
  $description: Fifth choice browser fallback. Leave blank to skip.
- excludeProcesses: "csgo.exe;valorant.exe;r5apex.exe;fortniteclient-win64-shipping.exe;overwatch.exe;gta5.exe;eldenring.exe"
  $name: Anti-Cheat & Gaming Exclusion List
  $description: Semicolon-separated list of processes where this mod should completely disable itself to protect performance and avoid anti-cheat flags.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>

std::vector<std::wstring> g_priorityBrowsers;
std::vector<std::wstring> g_excludedProcesses;
thread_local bool t_inHook = false; 

// Memory-friendly trimming helper
std::wstring TrimString(const std::wstring& str) {
    size_t first = str.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    size_t last = str.find_last_not_of(L" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Single-Pass converging search algorithm
std::wstring GetHighestPriorityRunningBrowser() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return L"";

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    
    size_t highestIndex = g_priorityBrowsers.size();
    std::wstring bestMatch = L"";

    if (Process32FirstW(hSnap, &pe)) {
        do {
            for (size_t i = 0; i < highestIndex; ++i) {
                if (_wcsicmp(pe.szExeFile, g_priorityBrowsers[i].c_str()) == 0) {
                    highestIndex = i;
                    bestMatch = g_priorityBrowsers[i];
                    break; 
                }
            }
            // Optimization: If the absolute #1 ranked browser is active, stop scanning immediately
            if (highestIndex == 0) break; 
            
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return bestMatch;
}

std::wstring GetCurrentProcessName() {
    WCHAR path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
        std::wstring sPath(path);
        size_t pos = sPath.find_last_of(L"\\/");
        if (pos != std::wstring::npos) return sPath.substr(pos + 1);
        return sPath;
    }
    return L"UNKNOWN";
}

void ParseSemicolonList(PCWSTR settingName, std::vector<std::wstring>& targetVector) {
    targetVector.clear();
    PCWSTR rawSetting = Wh_GetStringSetting(settingName);
    
    if (rawSetting && wcslen(rawSetting) > 0) {
        std::wstring s(rawSetting);
        std::wstringstream ss(s);
        std::wstring item;
        while (std::getline(ss, item, L';')) {
            std::wstring trimmed = TrimString(item);
            if (!trimmed.empty()) targetVector.push_back(trimmed);
        }
        Wh_FreeStringSetting(rawSetting);
    }
}

void LoadSettings() {
    g_priorityBrowsers.clear();

    const WCHAR* browserKeys[] = { L"browser1", L"browser2", L"browser3", L"browser4", L"browser5" };
    const std::wstring hardcodedDefaults[] = { L"brave.exe", L"firefox.exe", L"chrome.exe", L"msedge.exe", L"" };

    // Process each ranked UI textbox in order
    for (int i = 0; i < 5; ++i) {
        PCWSTR rawBrowser = Wh_GetStringSetting(browserKeys[i]);
        if (rawBrowser) {
            std::wstring trimmed = TrimString(rawBrowser);
            if (!trimmed.empty()) {
                g_priorityBrowsers.push_back(trimmed);
            }
            Wh_FreeStringSetting(rawBrowser);
        } else if (!hardcodedDefaults[i].empty()) {
            // Safe fallback if UI values aren't initialized yet
            g_priorityBrowsers.push_back(hardcodedDefaults[i]);
        }
    }

    // Ultimate fallback safety net if the user clears out all 5 boxes completely
    if (g_priorityBrowsers.empty()) {
        g_priorityBrowsers = { L"brave.exe", L"firefox.exe", L"chrome.exe", L"msedge.exe" };
    }

    // Load exclusions
    ParseSemicolonList(L"excludeProcesses", g_excludedProcesses);
}

// Global engine for standard Win32 execution redirection
bool RouteLinkIfNecessary(const WCHAR* lpFile, const WCHAR* lpParameters, int nShow) {
    if (!lpFile || t_inHook) return false;

    // Fast-path evaluation without allocating memory buffers
    bool isLink = (_wcsnicmp(lpFile, L"http://", 7) == 0 || _wcsnicmp(lpFile, L"https://", 8) == 0);
    if (!isLink) return false;

    std::wstring currentProc = GetCurrentProcessName();
    std::wstring targetBrowser = GetHighestPriorityRunningBrowser();

    if (!targetBrowser.empty()) {
        if (_wcsicmp(currentProc.c_str(), targetBrowser.c_str()) == 0) {
            return false; // Prevent circular routing loops inside the target browser
        }

        std::wstring cleanUrl = lpFile;
        if (cleanUrl.length() >= 2 && cleanUrl.front() == L'"' && cleanUrl.back() == L'"') {
            cleanUrl = cleanUrl.substr(1, cleanUrl.length() - 2);
        }

        Wh_Log(L"[SmartRouter] Re-routing link to running selection: %s", targetBrowser.c_str());

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_FLAG_NO_UI;
        sei.lpFile = targetBrowser.c_str();         
        sei.lpParameters = cleanUrl.c_str(); 
        sei.nShow = nShow;
        
        t_inHook = true;
        BOOL success = ShellExecuteExW(&sei);
        t_inHook = false;

        if (success) return true;
    }
    return false;
}

// --- Hook 1: ShellExecuteExW ---
using ShellExecuteExW_t = decltype(&ShellExecuteExW);
ShellExecuteExW_t ShellExecuteExW_Original;

BOOL WINAPI ShellExecuteExW_Hook(LPSHELLEXECUTEINFOW pExecInfo) {
    if (pExecInfo && pExecInfo->lpFile) {
        if (RouteLinkIfNecessary(pExecInfo->lpFile, pExecInfo->lpParameters, pExecInfo->nShow)) {
            pExecInfo->hInstApp = (HINSTANCE)32; 
            return TRUE;
        }
    }
    return ShellExecuteExW_Original(pExecInfo);
}

// --- Hook 2: ShellExecuteW ---
using ShellExecuteW_t = decltype(&ShellExecuteW);
ShellExecuteW_t ShellExecuteW_Original;

HINSTANCE WINAPI ShellExecuteW_Hook(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShow) {
    if (RouteLinkIfNecessary(lpFile, lpParameters, nShow)) {
        return (HINSTANCE)32;
    }
    return ShellExecuteW_Original(hwnd, lpOperation, lpFile, lpParameters, lpDirectory, nShow);
}

// --- Hook 3: CreateProcessW (UWP / Core Broker Intercept) ---
using CreateProcessW_t = decltype(&CreateProcessW);
CreateProcessW_t CreateProcessW_Original;

BOOL WINAPI CreateProcessW_Hook(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags,
                                LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
                                LPPROCESS_INFORMATION lpProcessInformation) {
    if (t_inHook) {
        return CreateProcessW_Original(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
                                       bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
    }

    // CRITICAL PERFORMANCE FAST PATH: Raw pointer string checking. 
    bool targetMatched = false;
    if (lpCommandLine && wcsstr(lpCommandLine, L"zen.exe")) targetMatched = true;
    else if (lpApplicationName && wcsstr(lpApplicationName, L"zen.exe")) targetMatched = true;

    if (targetMatched && lpCommandLine && (wcsstr(lpCommandLine, L"http://") || wcsstr(lpCommandLine, L"https://"))) {
        
        std::wstring cmdLine(lpCommandLine);
        size_t urlPos = cmdLine.find(L"https://");
        if (urlPos == std::wstring::npos) urlPos = cmdLine.find(L"http://");

        if (urlPos != std::wstring::npos) {
            std::wstring extractedUrl = cmdLine.substr(urlPos);
            
            size_t cleanupQuote = extractedUrl.find(L'"');
            if (cleanupQuote != std::wstring::npos) extractedUrl = extractedUrl.substr(0, cleanupQuote);
            size_t cleanupSpace = extractedUrl.find(L' ');
            if (cleanupSpace != std::wstring::npos) extractedUrl = extractedUrl.substr(0, cleanupSpace);

            std::wstring targetBrowser = GetHighestPriorityRunningBrowser();

            if (!targetBrowser.empty() && _wcsicmp(targetBrowser.c_str(), L"zen.exe") != 0) {
                
                Wh_Log(L"[SmartRouter] CreateProcessW Intercepted! Diverting to: %s", targetBrowser.c_str());

                SHELLEXECUTEINFOW sei = { sizeof(sei) };
                sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
                sei.lpFile = targetBrowser.c_str();         
                sei.lpParameters = extractedUrl.c_str(); 
                sei.nShow = SW_SHOWNORMAL;
                
                t_inHook = true;
                BOOL ok = ShellExecuteExW(&sei);
                t_inHook = false;

                if (ok) {
                    if (lpProcessInformation) {
                        lpProcessInformation->hProcess = sei.hProcess;
                        lpProcessInformation->hThread = NULL; 
                        lpProcessInformation->dwProcessId = GetProcessId(sei.hProcess);
                        lpProcessInformation->dwThreadId = 0;
                    }
                    return TRUE;
                }
            }
        }
    }

    return CreateProcessW_Original(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
                                   bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

// --- Lifecycle Initialization ---
BOOL Wh_ModInit() {
    // Session 0 Bypass
    DWORD dwSessionId = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &dwSessionId) && dwSessionId == 0) {
        return FALSE; 
    }

    LoadSettings();

    // Gaming & Anti-Cheat Protection Blocklist
    std::wstring currentProc = GetCurrentProcessName();
    for (const auto& excluded : g_excludedProcesses) {
        if (_wcsicmp(currentProc.c_str(), excluded.c_str()) == 0) {
            return FALSE; 
        }
    }

    // Apply Hooks cleanly
    Wh_SetFunctionHook((void*)ShellExecuteExW, (void*)ShellExecuteExW_Hook, (void**)&ShellExecuteExW_Original);
    Wh_SetFunctionHook((void*)ShellExecuteW, (void*)ShellExecuteW_Hook, (void**)&ShellExecuteW_Original);
    Wh_SetFunctionHook((void*)CreateProcessW, (void*)CreateProcessW_Hook, (void**)&CreateProcessW_Original);

    return TRUE;
}

void Wh_ModUninit() {
    // Clean uninitialization layout
}

void Wh_ModSettingsChanged() {
    LoadSettings();
}