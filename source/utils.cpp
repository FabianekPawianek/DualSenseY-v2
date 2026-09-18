#include "utils.hpp"
#include <algorithm>
#include <mutex>
#include "log.hpp"
#ifdef WINDOWS
#include <Windows.h>
#include <cfgmgr32.h>
#include <vector>
#include <string>
#endif

#ifdef WINDOWS
static std::wstring Utf8ToWstring(const std::string& str)
{
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
    if (size_needed <= 0) return std::wstring();
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], size_needed);
    return wstr;
}

std::string GetDeviceInstancePath(const std::string& lastPath) {
    if (lastPath.empty()) return "";

    std::string path = lastPath;

    // Remove prefix "\\?\" or "\\.\"
    if (path.compare(0, 4, "\\\\?\\") == 0 || path.compare(0, 4, "\\\\.\\") == 0) {
        path.erase(0, 4);
    }

	// Look for last GUID (starts with '{' ) because BT devices have in-between GUID
    size_t lastHashGuid = path.rfind("#{");
    if (lastHashGuid != std::string::npos) {
        path.erase(lastHashGuid);
    } else {
        size_t lastSlashGuid = path.rfind("\\{");
        if (lastSlashGuid != std::string::npos && path.back() == '}' && (path.length() - lastSlashGuid) <= 40) {
            path.erase(lastSlashGuid);
        }
    }

    // Replace '#' with '\' for HID format
    std::replace(path.begin(), path.end(), '#', '\\');

    // Convert to uppercase (standard HID\VID_054C&PID_0CE6\... or USB\VID_...)
    std::transform(path.begin(), path.end(), path.begin(), ::toupper);

    return path;
}
#endif

bool ReplugDevice(const std::wstring& instanceId)
{
#ifdef WINDOWS
    DEVINST devInst;
    CONFIGRET status = CM_Locate_DevNodeW(
        &devInst,
        const_cast<LPWSTR>(instanceId.c_str()),
        CM_LOCATE_DEVNODE_NORMAL
    );

    if (status != CR_SUCCESS)
    {
        std::wcout << L"Device not found:\n";
        std::wcout << instanceId.c_str() << L"\n";
        return false;
    }

    CM_Disable_DevNode(devInst, 0);
    Sleep(200);
    CM_Enable_DevNode(devInst, 0);
#endif

    return true;
}


// Function to retrieve HidHide installation path from Windows registry
std::string getHidHideExecutablePath() {
#ifdef WINDOWS
    HKEY hKey = NULL;
    std::string hidHidePath = "";
    
    // Try to open the registry key for HidHide from Nefarius Software Solutions e.U.
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Nefarius Software Solutions e.U.\\HidHide", 0, KEY_READ, &hKey);
    
    // If not found, try the WOW6432Node (32-bit registry on 64-bit systems)
    if (result != ERROR_SUCCESS) {
        result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Nefarius Software Solutions e.U.\\HidHide", 0, KEY_READ, &hKey);
    }
    
    if (result == ERROR_SUCCESS) {
        DWORD dataSize = MAX_PATH;
        char installPath[MAX_PATH] = {0};
        
        // Try to read the Path value
        result = RegQueryValueExA(hKey, "Path", NULL, NULL, (LPBYTE)installPath, &dataSize);
        
        if (result == ERROR_SUCCESS && dataSize > 0) {
            hidHidePath = std::string(installPath);
            // Ensure proper path formatting and append the x64 executable path
            if (hidHidePath.back() != '\\') {
                hidHidePath += "\\";
            }
            hidHidePath += "x64\\HidHideCLI.exe";
            LOGI("Found HidHide at: %s", hidHidePath.c_str());
        }
        
        RegCloseKey(hKey);
    } else {
        // Fallback: Try common installation paths from Nefarius Software Solutions
        std::vector<std::string> commonPaths = {
            "C:\\Program Files\\Nefarius Software Solutions\\HidHide\\x64\\HidHideCLI.exe",
            "C:\\Program Files\\Nefarius Software Solutions\\HidHide\\x86\\HidHideCLI.exe",
            "C:\\Program Files (x86)\\Nefarius Software Solutions\\HidHide\\x86\\HidHideCLI.exe"
        };
        
        for (const auto& path : commonPaths) {
            if (std::filesystem::exists(path)) {
                hidHidePath = path;
                LOGI("Found HidHide at common path: %s", hidHidePath.c_str());
                break;
            }
        }
    }
    
#if 0
    // If not found, show alert message
    if (hidHidePath.empty()) {
        std::string errorMsg = "HidHide is not installed on your system.\n\n"
            "Please install HidHide from: https://github.com/nefarius/HidHide\n\n"
            "Without HidHide, the controller hiding feature will not work.";
        LOGE("%s", errorMsg.c_str());
        MessageBoxA(NULL, errorMsg.c_str(), "HidHide Not Found", MB_OK | MB_ICONWARNING);
    }
#endif // 0

    
    return hidHidePath;
#else
    return "";
#endif
}

void HidHideRequest(std::string ID, std::string arg) {
#ifdef WINDOWS
    if (ID.empty()) {
        return;
    }

    // Get HidHide executable path
    std::string hidHideExePath = getHidHideExecutablePath();
    if (hidHideExePath.empty()) {
        LOGE("HidHide executable not found. Controller hiding feature is disabled.");
        return;
    }

    std::string hidDeviceInstancePath = GetDeviceInstancePath(ID);
    if (hidDeviceInstancePath.empty()) {
        return;
    }

    // Build command based on the requested action
    std::string command = "\"" + hidHideExePath + "\"";

    if (arg == "hide") {
        // Ensure application is registered before hiding so DualSenseY doesn't lose access
        RegisterApplicationWithHidHide();
        command += " --cloak-on --dev-hide \"" + hidDeviceInstancePath + "\"";
        LOGI("Executing HidHide: hide device %s", hidDeviceInstancePath.c_str());
    }
    else if (arg == "show") {
        command += " --dev-unhide \"" + hidDeviceInstancePath + "\" --cloak-off";
        LOGI("Executing HidHide: unhide device %s and disable cloak", hidDeviceInstancePath.c_str());
    }
    else {
        LOGE("Invalid argument for HidHideRequest. Only 'hide' and 'show' are supported.");
        return;
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmdBuffer(command.begin(), command.end());
    cmdBuffer.push_back('\0');

    // Execute the HidHide CLI command
    if (CreateProcessA(NULL,
        cmdBuffer.data(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi)) {
        // Wait for the process to complete
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        // Get exit code to verify success
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        
        if (exitCode == 0) {
            LOGI("HidHide command completed successfully");
        } else {
            LOGE("HidHide command failed with exit code: %lu", static_cast<unsigned long>(exitCode));
        }
        
        // Close process and thread handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else {
        LOGE("Failed to execute HidHide command. Error: %lu", static_cast<unsigned long>(GetLastError()));
    }
#endif
}

void hidHideRequest(std::string ID, std::string arg) {
    HidHideRequest(ID, arg);
}

void DisableHidHideCloak() {
#ifdef WINDOWS
    std::string hidHideExePath = getHidHideExecutablePath();
    if (hidHideExePath.empty()) return;

    std::string command = "\"" + hidHideExePath + "\" --cloak-off";

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmdBuffer(command.begin(), command.end());
    cmdBuffer.push_back('\0');

    if (CreateProcessA(NULL,
        cmdBuffer.data(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        LOGI("HidHide cloaking disabled globally");
    }
#endif
}

// Function to get the path of the current executable
std::string getCurrentExecutablePath() {
#ifdef WINDOWS
    std::vector<char> buffer(MAX_PATH);
    DWORD size;

    while (true) {
        size = GetModuleFileNameA(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));
        
        if (size == 0) return ""; // Error
        
        // If buffer small, make it bigger and try again
        if (size == buffer.size()) {
            buffer.resize(buffer.size() * 2);
        } else {
            break; // Success
        }
    }
    return std::string(buffer.data(), size);
#else
    return "";
#endif
}

// Function to register this application with HidHide so it can always see hidden devices
void RegisterApplicationWithHidHide() {
#ifdef WINDOWS
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&pi, sizeof(pi));

    std::string appPath = getCurrentExecutablePath();
    if (appPath.empty()) {
        LOGE("Failed to get current executable path for HidHide registration");
        return;
    }

    // Get HidHide executable path
    std::string hidHideExePath = getHidHideExecutablePath();
    if (hidHideExePath.empty()) {
        LOGE("HidHide executable not found. Cannot register application.");
        return;
    }

    // Build command to register this application: HidHideCLI.exe --app-reg "<app_path>"
    std::string command = "\"" + hidHideExePath + "\" --app-reg \"" + appPath + "\"";

    std::vector<char> cmdBuffer(command.begin(), command.end());
    cmdBuffer.push_back('\0');

    if (CreateProcessA(NULL,
        cmdBuffer.data(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi)) {
        // Wait for the process to complete
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        
        if (exitCode == 0) {
            LOGI("Application successfully registered with HidHide: %s", appPath.c_str());
        } else {
            LOGE("Failed to register application with HidHide. Exit code: %lu", static_cast<unsigned long>(exitCode));
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else {
        LOGE("Failed to execute HidHide registration command. Error: %lu", static_cast<unsigned long>(GetLastError()));
    }
#endif
}

std::string USBtoHIDinstance(const std::string& input) {
    return GetDeviceInstancePath(input);
}

void HideController(const std::string& instanceId) {
#if !defined(__linux__) && !defined(__MACOS__)
    hidHideRequest(instanceId, "hide");
#endif
}

void UnhideController(const std::string& instanceId) {
#if !defined(__linux__) && !defined(__MACOS__)
    hidHideRequest(instanceId, "show");
#endif
}

bool IsRunningAsAdministratorWindows() {
#if !defined(__linux__) && !defined(__MACOS__)
    BOOL fIsRunAsAdmin = FALSE;
    DWORD dwError = ERROR_SUCCESS;
    PSID pAdministratorsGroup = NULL;

    // Allocate and initialize a SID of the administrators group.
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(
        &NtAuthority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &pAdministratorsGroup)) {
        dwError = GetLastError();
        goto Cleanup;
    }

    // Determine whether the SID of administrators group is enabled in 
    // the primary access token of the process.
    if (!CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin)) {
        dwError = GetLastError();
        goto Cleanup;
    }

Cleanup:
    // Centralized cleanup for all allocated resources.
    if (pAdministratorsGroup) {
        FreeSid(pAdministratorsGroup);
        pAdministratorsGroup = NULL;
    }

    // Throw the error if something failed in the function.
    if (ERROR_SUCCESS != dwError) {
        throw dwError;
    }

    return fIsRunAsAdmin;
#endif
    return false;
}

void DisableBluetoothDevice(const std::string& Address) {
    if (Address == "")
        return;

#ifdef WINDOWS
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::string cleanMac = Address;
    cleanMac.erase(std::remove(cleanMac.begin(), cleanMac.end(), ':'), cleanMac.end());

    std::string command = RESOURCES_PATH "externals/windows/BTControl.exe " + cleanMac;
    LOGI("BTControl command: %s", command.c_str());

    if (CreateProcess(NULL,
        (LPSTR)command.c_str(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi)
    ) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#endif
}

#ifdef WINDOWS
static bool RunHiddenCommand(const std::string& command, DWORD* outExitCode = nullptr) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmdBuffer(command.begin(), command.end());
    cmdBuffer.push_back('\0');

    if (!CreateProcessA(NULL, cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        LOGE("Failed to execute command: %s (Error: %lu)", command.c_str(), static_cast<unsigned long>(GetLastError()));
        return false;
    }

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (outExitCode) *outExitCode = exitCode;
    return (exitCode == 0);
}

static bool RunHiddenCommandWithOutput(const std::string& command, std::string& outOutput, DWORD* outExitCode = nullptr) {
    HANDLE hReadPipe = NULL;
    HANDLE hWritePipe = NULL;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmdBuffer(command.begin(), command.end());
    cmdBuffer.push_back('\0');

    if (!CreateProcessA(NULL, cmdBuffer.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return false;
    }

    CloseHandle(hWritePipe);

    char buffer[1024];
    DWORD bytesRead = 0;
    outOutput.clear();
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        outOutput.append(buffer, bytesRead);
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (outExitCode) *outExitCode = exitCode;
    return (exitCode == 0);
}
#endif

bool SetAutostartWindows(bool enable, bool delayEnabled, int delaySeconds, bool startAsAdmin) {
#ifdef WINDOWS
    if (enable) {
        std::string exePath = getCurrentExecutablePath();
        if (exePath.empty()) {
            LOGE("Cannot determine executable path for autostart");
            return false;
        }

        std::string args = " --minimized";
        if (delayEnabled && delaySeconds > 0) {
            args += " --delay " + std::to_string(delaySeconds);
        }

        if (startAsAdmin) {
            // 1. Remove registry entry if exists to prevent duplicate launch
            HKEY hKey = NULL;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueA(hKey, "DualSenseY");
                RegCloseKey(hKey);
            }

            // 2. Create Task Scheduler task with HIGHEST privileges
            std::string createCmd = "schtasks.exe /Create /TN \"DualSenseY\" /TR \"\\\"" + exePath + "\\\"" + args + "\" /SC ONLOGON /RL HIGHEST /F";
            DWORD exitCode = 0;
            bool ok = RunHiddenCommand(createCmd, &exitCode);
            if (!ok || exitCode != 0) {
                LOGE("Failed to create scheduled task for DualSenseY. Exit code: %lu", static_cast<unsigned long>(exitCode));
                return false;
            }
            LOGI("DualSenseY scheduled task autostart enabled (Admin): %s%s", exePath.c_str(), args.c_str());
            return true;
        } else {
            // 1. Remove Task Scheduler task if exists
            RunHiddenCommand("schtasks.exe /Delete /TN \"DualSenseY\" /F");

            // 2. Add entry to Registry Run key
            HKEY hKey = NULL;
            LONG result = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);
            if (result != ERROR_SUCCESS) {
                LOGE("Failed to open Run registry key: %ld", result);
                return false;
            }

            std::string cmd = "\"" + exePath + "\"" + args;
            result = RegSetValueExA(hKey, "DualSenseY", 0, REG_SZ, (const BYTE*)cmd.c_str(), static_cast<DWORD>(cmd.length() + 1));
            RegCloseKey(hKey);

            if (result != ERROR_SUCCESS) {
                LOGE("Failed to set DualSenseY autostart registry value: %ld", result);
                return false;
            }
            LOGI("DualSenseY autostart enabled in registry: %s", cmd.c_str());
            return true;
        }
    } else {
        // Disable autostart: clean both Task Scheduler and Registry
        HKEY hKey = NULL;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueA(hKey, "DualSenseY");
            RegCloseKey(hKey);
        }

        RunHiddenCommand("schtasks.exe /Delete /TN \"DualSenseY\" /F");
        LOGI("DualSenseY autostart disabled");
        return true;
    }
#else
    return false;
#endif
}

bool GetAutostartWindows(bool& outEnabled, bool& outDelayEnabled, int& outDelaySeconds, bool& outStartAsAdmin) {
#ifdef WINDOWS
    outEnabled = false;
    outDelayEnabled = false;
    outDelaySeconds = 15;
    outStartAsAdmin = true;

    // 1. Check Task Scheduler
    std::string taskOutput;
    DWORD taskExitCode = 1;
    if (RunHiddenCommandWithOutput("schtasks.exe /Query /TN \"DualSenseY\" /FO CSV /V", taskOutput, &taskExitCode) && taskExitCode == 0) {
        outEnabled = true;
        outStartAsAdmin = true;

        size_t delayPos = taskOutput.find("--delay");
        if (delayPos != std::string::npos) {
            outDelayEnabled = true;
            try {
                std::string sub = taskOutput.substr(delayPos + 7);
                size_t start = sub.find_first_not_of(" = \t\"");
                if (start != std::string::npos) {
                    outDelaySeconds = std::clamp(std::stoi(sub.substr(start)), 5, 60);
                }
            } catch (...) {
                outDelaySeconds = 15;
            }
        }
        return true;
    }

    // 2. Check Registry Run key
    HKEY hKey = NULL;
    LONG result = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        char buffer[1024] = {0};
        DWORD size = sizeof(buffer);
        result = RegQueryValueExA(hKey, "DualSenseY", NULL, NULL, (LPBYTE)buffer, &size);
        RegCloseKey(hKey);

        if (result == ERROR_SUCCESS) {
            outEnabled = true;
            outStartAsAdmin = false;
            std::string cmd(buffer);
            size_t delayPos = cmd.find("--delay");
            if (delayPos != std::string::npos) {
                outDelayEnabled = true;
                try {
                    std::string sub = cmd.substr(delayPos + 7);
                    size_t start = sub.find_first_not_of(" = \t");
                    if (start != std::string::npos) {
                        outDelaySeconds = std::clamp(std::stoi(sub.substr(start)), 5, 60);
                    }
                } catch (...) {
                    outDelaySeconds = 15;
                }
            }
            return true;
        }
    }

    // Neither found: default outStartAsAdmin to true
    outEnabled = false;
    outStartAsAdmin = true;
    outDelayEnabled = false;
    outDelaySeconds = 15;
    return true;
#else
    outEnabled = false;
    outDelayEnabled = false;
    outDelaySeconds = 15;
    outStartAsAdmin = false;
    return false;
#endif
}

bool GetAutostartWindows(bool& outEnabled, bool& outDelayEnabled, int& outDelaySeconds) {
    bool unusedAdmin = true;
    return GetAutostartWindows(outEnabled, outDelayEnabled, outDelaySeconds, unusedAdmin);
}

#ifdef WINDOWS
static std::string g_LastExternalProcess = "";
static std::mutex g_ProcessMutex;

std::string GetForegroundProcessName() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return "";

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return "";

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return "";

    char pathBuffer[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    std::string exeName = "";
    if (QueryFullProcessImageNameA(hProcess, 0, pathBuffer, &size)) {
        std::string fullPath(pathBuffer);
        size_t lastSlash = fullPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeName = fullPath.substr(lastSlash + 1);
        } else {
            exeName = fullPath;
        }
    }
    CloseHandle(hProcess);

    if (pid != GetCurrentProcessId() && !exeName.empty()) {
        std::lock_guard<std::mutex> lock(g_ProcessMutex);
        g_LastExternalProcess = exeName;
    }

    return exeName;
}

std::string GetLastExternalProcessName() {
    std::lock_guard<std::mutex> lock(g_ProcessMutex);
    return g_LastExternalProcess;
}

bool IsCurrentProcess(const std::string& processName) {
    if (processName.empty()) return false;
    std::string appPath = getCurrentExecutablePath();
    if (appPath.empty()) return false;
    size_t lastSlash = appPath.find_last_of("\\/");
    std::string currentExe = (lastSlash != std::string::npos) ? appPath.substr(lastSlash + 1) : appPath;
    if (processName.length() != currentExe.length()) return false;
    for (size_t i = 0; i < processName.length(); ++i) {
        if (std::tolower(static_cast<unsigned char>(processName[i])) !=
            std::tolower(static_cast<unsigned char>(currentExe[i]))) {
            return false;
        }
    }
    return true;
}

bool IsNativeDualSenseGame(const std::string& processName, const std::vector<std::string>& gamesList) {
    if (processName.empty()) return false;
    for (const auto& game : gamesList) {
        if (game.length() == processName.length()) {
            bool match = true;
            for (size_t i = 0; i < game.length(); ++i) {
                if (std::tolower(static_cast<unsigned char>(game[i])) !=
                    std::tolower(static_cast<unsigned char>(processName[i]))) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
    }
    return false;
}
#else
std::string GetForegroundProcessName() { return ""; }
std::string GetLastExternalProcessName() { return ""; }
bool IsCurrentProcess(const std::string& processName) { return false; }
bool IsNativeDualSenseGame(const std::string& processName, const std::vector<std::string>& gamesList) { return false; }
#endif


