#ifndef UTILS_H
#define UTILS_H

#include <string>
#include "log.hpp"
#include <filesystem>
#include <iostream>

#include <vector>

void HidHideRequest(std::string ID, std::string arg);
void hidHideRequest(std::string ID, std::string arg);
std::string USBtoHIDinstance(const std::string& input);
std::string GetDeviceInstancePath(const std::string& lastPath);
void HideController(const std::string& instanceId);
void UnhideController(const std::string& instanceId);
void RegisterApplicationWithHidHide();  // Register DualsenseY application With HidHide to see under the cloak
bool IsRunningAsAdministratorWindows();  
void DisableBluetoothDevice(const std::string& Address);
std::string getHidHideExecutablePath();
void DisableHidHideCloak();

bool SetAutostartWindows(bool enable, bool delayEnabled, int delaySeconds);
bool GetAutostartWindows(bool& outEnabled, bool& outDelayEnabled, int& outDelaySeconds);

std::string GetForegroundProcessName();
std::string GetLastExternalProcessName();
bool IsCurrentProcess(const std::string& processName);
bool IsNativeDualSenseGame(const std::string& processName, const std::vector<std::string>& gamesList);

#endif