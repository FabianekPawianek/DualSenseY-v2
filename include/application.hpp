#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include <memory>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "scePadSettings.hpp"
#include "appSettings.hpp"
#include <thread>
#include <tray.hpp>
#include "batteryIconRenderer.hpp"
#include "strings.hpp"

#include <chrono>

constexpr auto WIN32_MSG_WINDOW_MUTEX = "DSYMSG";

struct glfwDeleter {
	void operator()(GLFWwindow* window) {
		glfwDestroyWindow(window);
	}
};

class Application {
private:
	std::unique_ptr<GLFWwindow, glfwDeleter> m_GlfwWindow;
	s_scePadSettings m_ScePadSettings[4] = {};
	bool IsMinimized();
	void DisableControllerInputIfMinimized();
	AppSettings m_AppSettings = {};
	static void IconifyCallback(GLFWwindow* window, int iconified);
	std::unique_ptr<Tray::Tray> m_Tray;
	std::thread m_TrayThread;

#if defined(_WIN32)
	BatteryIconRenderer m_BatteryIconRenderer;
	HICON m_CurrentTrayIcon = nullptr;
	bool m_HasNotifiedLowBattery[4] = { false, false, false, false };
	bool m_WasConnected[4] = { false, false, false, false };
	std::chrono::steady_clock::time_point m_ConnectionStartTime[4] = {};
	std::chrono::steady_clock::time_point m_ZeroBatteryStartTime[4] = {};
	uint8_t m_LastTrayBatteryLevel = 255;
	int m_LastTrayBatteryBracket = -1;
	bool m_LastTrayCharging = false;
	bool m_LastTrayConnected = false;
	bool m_LastTrayLightTheme = false;
	int m_LastTrayIconSize = 0;
	std::chrono::steady_clock::time_point m_LastTrayUpdateTime = {};
	void UpdateTrayBatteryStatus(int controllerIndex, Strings& strings);
#endif
public:
	enum class Platform {
		Windows,
		Linux,
		Android,
		Unknown
	};

	inline Platform GetPlatform() {
	#if defined(__linux__)
		return Platform::Linux;
	#elif defined(__ANDROID__)
		return Platform::Android;
	#elif defined(_WIN32)
		return Platform::Windows;
	#else
		return Platform::Unknown;
	#endif
	}
	
	bool Run(const std::string& Argument1 = "");
	void InitializeWindow();
	void SetStyleAndColors();
	void SetupTray();
	void HideWindowToTray();
	void RestoreWindowFromTray();
	Application() = default;
	~Application();
};


#endif // APPLICATION_HPP