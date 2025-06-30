#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Commctrl.h>
#include <stdio.h>
#include <string>

#pragma comment(lib, "Comctl32.lib")

HMODULE g_hDll = NULL;

bool RegisterMreExtension()
{
    bool dirty = false;

    // 1) get EXE path
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, _countof(exePath)))
        return false;

    // 2) build two strings
    std::wstring iconSpec = exePath; iconSpec += L",0";
    std::wstring openCmd = L"\""; openCmd += exePath; openCmd += L"\" \"%1\"";

    HKEY  hKey;
    LONG  r;
    DWORD type, cb;

    // --- .mre → ProgID MemRE.Table ---
    constexpr wchar_t const* MRE_EXT = L"Software\\Classes\\.mre";
    bool needProgID = true;
    if ((r = RegOpenKeyExW(HKEY_CURRENT_USER, MRE_EXT, 0, KEY_READ, &hKey)) == ERROR_SUCCESS)
    {
        wchar_t buf[32] = {};
        cb = sizeof(buf);
        if (RegQueryValueExW(hKey, nullptr, nullptr, &type, (BYTE*)buf, &cb) == ERROR_SUCCESS
            && type == REG_SZ
            && wcscmp(buf, L"MemRE.Table") == 0)
        {
            needProgID = false;
        }
        RegCloseKey(hKey);
    }
    if (needProgID)
    {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, MRE_EXT, 0, nullptr, 0,
            KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                (BYTE*)L"MemRE.Table",
                DWORD((wcslen(L"MemRE.Table") + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            dirty = true;
        }
    }

    // --- MemRE.Table\DefaultIcon → iconSpec ---
    constexpr wchar_t const* ICON_KEY = L"Software\\Classes\\MemRE.Table\\DefaultIcon";
    bool needIcon = true;
    if ((r = RegOpenKeyExW(HKEY_CURRENT_USER, ICON_KEY, 0, KEY_READ, &hKey)) == ERROR_SUCCESS)
    {
        cb = 0;
        if (RegQueryValueExW(hKey, nullptr, nullptr, &type, nullptr, &cb) == ERROR_SUCCESS
            && type == REG_SZ)
        {
            std::wstring existing; existing.resize(cb / sizeof(wchar_t));
            if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr,
                (BYTE*)existing.data(), &cb) == ERROR_SUCCESS)
            {
                if (!existing.empty() && existing.back() == L'\0')
                    existing.pop_back();
                if (existing == iconSpec) needIcon = false;
            }
        }
        RegCloseKey(hKey);
    }
    if (needIcon)
    {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, ICON_KEY, 0, nullptr, 0,
            KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                (BYTE*)iconSpec.c_str(),
                DWORD((iconSpec.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            dirty = true;
        }
    }

    // --- MemRE.Table\Shell\Open\Command → openCmd ---
    constexpr wchar_t const* CMD_KEY = L"Software\\Classes\\MemRE.Table\\Shell\\Open\\Command";
    bool needCmd = true;
    if ((r = RegOpenKeyExW(HKEY_CURRENT_USER, CMD_KEY, 0, KEY_READ, &hKey)) == ERROR_SUCCESS)
    {
        cb = 0;
        if (RegQueryValueExW(hKey, nullptr, nullptr, &type, nullptr, &cb) == ERROR_SUCCESS
            && type == REG_SZ)
        {
            std::wstring existing; existing.resize(cb / sizeof(wchar_t));
            if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr,
                (BYTE*)existing.data(), &cb) == ERROR_SUCCESS)
            {
                if (!existing.empty() && existing.back() == L'\0')
                    existing.pop_back();
                if (existing == openCmd) needCmd = false;
            }
        }
        RegCloseKey(hKey);
    }
    if (needCmd)
    {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, CMD_KEY, 0, nullptr, 0,
            KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
        {
            RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                (BYTE*)openCmd.c_str(),
                DWORD((openCmd.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            dirty = true;
        }
    }

    // only tell Explorer to reload if we actually changed something
    if (dirty)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return dirty;
}

BOOL WINAPI CtrlHandler(DWORD dwCtrlType)
{
    switch (dwCtrlType)
    {
    case CTRL_CLOSE_EVENT:
        if (g_hDll)
            FreeLibrary(g_hDll);
        return FALSE;
    default:
        return FALSE;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    static bool alreadyRegistered = false;
    if (!alreadyRegistered)
    {
        alreadyRegistered = RegisterMreExtension();
    }

    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icex);

    wchar_t exePath[MAX_PATH] = { 0 };
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH))
    {
        MessageBoxW(NULL, L"Failed to get the executable path.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::wstring exeDir(exePath);
    size_t pos = exeDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        exeDir = exeDir.substr(0, pos + 1);
    else
        exeDir = L".\\";

    SetCurrentDirectoryW(exeDir.c_str());

    // find and load MemRE DLL
    std::wstring searchPattern = exeDir + L"*.dll";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(NULL, L"No DLL found in the executable's directory.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::wstring dllFileName = findData.cFileName;
    FindClose(hFind);

    std::wstring dllFullPath = exeDir + dllFileName;
    HMODULE hDll = LoadLibraryW(dllFullPath.c_str());
    if (hDll == NULL)
    {
        std::wstring errorMsg = L"Failed to load DLL:\n" + dllFullPath;
        MessageBoxW(NULL, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    g_hDll = hDll;

    // If launched with a .mre path, forward it via WM_COPYDATA
    if (lpCmdLine && *lpCmdLine)
    {
        std::wstring path(lpCmdLine);

        if (path.size() >= 2 && path.front() == L'\"' && path.back() == L'\"')
            path = path.substr(1, path.size() - 2);

        Sleep(100);

        // find the main window
        HWND hMain = FindWindowW(L"MemoryScannerClass", nullptr);
        if (hMain)
        {
            COPYDATASTRUCT cds = {};
            cds.dwData = 1;
            cds.cbData = static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t));
            cds.lpData = (PVOID)path.c_str();
            SendMessageW(hMain, WM_COPYDATA, 0, (LPARAM)&cds);
        }
    }
    // ─────────────────────────────────────────────────────────────────────────────

    //Show console (minimized)
    AllocConsole();
    HWND hConsole = GetConsoleWindow();
    if (hConsole)
        ShowWindow(hConsole, SW_MINIMIZE);

    FILE* fConsole = nullptr;
    freopen_s(&fConsole, "CONOUT$", "w", stdout);
    wprintf(L"This console must stay open while MemRE is running...\n");

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    Sleep(INFINITE);

    FreeLibrary(hDll);
    FreeConsole();
    return 0;
}
