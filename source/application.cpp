#define NOMINMAX
#include "application.hpp"
#include "log.hpp"

#if defined(_WIN32) && (!defined(__linux__) && !defined(__APPLE__))
#include <Windows.h>
#include <consoleapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <win32WindowStuff.hpp>
#include <winuser.h>
#endif

#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <imgui.h>
#include <duaLib.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_glfw.h>
#include <stb_image/stb_image.h>
#include <algorithm>

#include "mainWindow.hpp"
#include "strings.hpp"
#include "audioPassthrough.hpp"
#include "udp.hpp"
#include "controllerEmulation.hpp"
#include "scePadHandle.hpp"
#include "keyboardMouseMapper.hpp"
#include "client.hpp"
#include "appMutex.hpp"
#include "utils.hpp"

bool isLightMode = false;
#if !defined(__linux__) && !defined(__MACOS__)
bool IsWindowsLightMode() {
	DWORD value = 1;
	DWORD size = sizeof(value);
	if (RegGetValueW(
		HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"AppsUseLightTheme",
		RRF_RT_DWORD,
		nullptr,
		&value,
		&size
		) != ERROR_SUCCESS)
		return true;
	return value != 0;
}

bool colorsChanged = false;
bool winSettingChange = false;
WNDPROC originalWndProc = nullptr;
LRESULT CALLBACK CustomWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_DWMCOLORIZATIONCOLORCHANGED:
		{
			colorsChanged = true;
			return 0;
		}
		case WM_SETTINGCHANGE:
		{
			winSettingChange = true;
			return 0;
		}
		default:
			return CallWindowProc(originalWndProc, hwnd, msg, wParam, lParam);
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif

bool Application::IsMinimized() {
	if (!m_GlfwWindow) return true;
#ifdef WINDOWS
	HWND hwnd = glfwGetWin32Window(m_GlfwWindow.get());
	if (hwnd) {
		bool isForeground = (GetForegroundWindow() == hwnd);
		bool isIconic = IsIconic(hwnd) != 0;
		bool isVisible = IsWindowVisible(hwnd) != 0;
		if (!isForeground || isIconic || !isVisible) {
			return true;
		}
	}
#endif
	bool isMinimized = glfwGetWindowAttrib(m_GlfwWindow.get(), GLFW_ICONIFIED) != 0;
	bool isFocused = glfwGetWindowAttrib(m_GlfwWindow.get(), GLFW_FOCUSED) != 0;
	return (isMinimized || !isFocused);
}

void Application::DisableControllerInputIfMinimized() {
	ImGuiIO& io = ImGui::GetIO();

	if (IsMinimized()) {
		if (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) {
			io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
			io.ClearInputKeys();
		}
	}
	else {
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	}
}

void Application::IconifyCallback(GLFWwindow* window, int iconified) {
	Application* application = static_cast<Application*>(glfwGetWindowUserPointer(window));
	if (application->m_AppSettings.HideToTrayOnMinimize && iconified) {
		application->HideWindowToTray();
	}
}

bool Application::Run(const std::string& Argument1, bool startMinimized) {
	// Delete update.zip if present
	remove("update.zip");

	Platform platform = Application::GetPlatform();
#pragma region Initialize duaLib
	s_ScePadInitParam initParam = {};
	initParam.allowBT = true;
	scePadInit3(&initParam);
	g_ScePad[0] = scePadOpen(1, 0, 0);
	g_ScePad[1] = scePadOpen(2, 0, 0);
	g_ScePad[2] = scePadOpen(3, 0, 0);
	g_ScePad[3] = scePadOpen(4, 0, 0);
	printf("%d", g_ScePad[0]);
	scePadSetParticularMode(true);
#pragma endregion

#pragma region Initialize application
	LoadAppSettings(&m_AppSettings);
	SaveAppSettings(&m_AppSettings); // Save here so it updates
	UDP udp(m_AppSettings.LocalPort);
	if (udp.IsConnectedInsteadOfBinded()) {
			
		if (Argument1 != "") udp.SendConfigPathToAnotherInstance(Argument1);
		else udp.BringOtherInstanceToFront();
		
		std::exit(0);
	}

	AudioPassthrough audio = {};
	Vigem vigem(m_ScePadSettings, udp);
	Strings strings = {};
	KeyboardMouseMapper keyboardMouseMapper(m_ScePadSettings);
	Client client(m_ScePadSettings);
	client.Start();
	if (!m_AppSettings.DontConnectToServerOnStart) client.Connect(m_AppSettings.ServerAddress, m_AppSettings.ServerPort);
	client.AllowedToHostController = vigem.IsVigemConnected();
	vigem.SetPeerControllerDataPointer(client.GetActivePeerControllerMap());
	strings.ReadStringsFromJson(CountryCodeToFile(m_AppSettings.SelectedLanguage));

	if (Argument1 != "") {
		ForceControllerToNotLoadDefault(0);
		LoadSettingsFromFile(&m_ScePadSettings[0], Argument1);
	}

	bool shouldStartHidden = startMinimized || m_AppSettings.HideToTrayOnStart;
	InitializeWindow(shouldStartHidden);
	SetupTray();

	ImGuiIO& io = ImGui::GetIO();
	if (shouldStartHidden) HideWindowToTray();
	io.FontDefault = io.Fonts->Fonts[g_FontIndex[m_AppSettings.SelectedLanguage]];

	// Windows
	MainWindow main(strings, audio, vigem, udp, m_AppSettings, client, isLightMode);
#pragma endregion

#ifdef WINDOWS
	HANDLE hTimer = CreateWaitableTimer(NULL, TRUE, NULL);
	LARGE_INTEGER liDueTime;
	liDueTime.QuadPart = -100000LL;
#endif

#ifdef WINDOWS
	RegisterApplicationWithHidHide();
#endif

	int display_w, display_h = 0;
	float xscale, yscale = 1;
	bool active = false;
	uint32_t occasionalFrameWhenMinimized = 0;
	while (!glfwWindowShouldClose(m_GlfwWindow.get())) {
		glfwPollEvents();
		DisableControllerInputIfMinimized();

		bool v_isMinimized = IsMinimized();
		occasionalFrameWhenMinimized = v_isMinimized ? occasionalFrameWhenMinimized + 1 : 0;

	#pragma region ImGUI
		bool finishFrame = false;
		if (v_isMinimized && occasionalFrameWhenMinimized > 500 || !v_isMinimized) {
			glClear(GL_COLOR_BUFFER_BIT);
			glClearColor(0, 0, 0, 0);
			glfwGetFramebufferSize(m_GlfwWindow.get(), &display_w, &display_h);
			glViewport(0, 0, display_w, display_h);
			glfwGetWindowContentScale(m_GlfwWindow.get(), &xscale, &yscale);

		#if !defined(__linux__) && !defined(__MACOS__)
			if (colorsChanged || winSettingChange) {
				SetStyleAndColors();
				colorsChanged = false;
				winSettingChange = false;
			}
		#endif

			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			io.FontGlobalScale = xscale + 0.5;
			finishFrame = true;
		}
	#pragma endregion

		int selectedController = main.GetSelectedController();
#if defined(_WIN32)
		UpdateTrayBatteryStatus(selectedController, strings);
#endif
		vigem.SetSelectedController(selectedController);
		client.SetSelectedController(selectedController);
		udp.SetVibrationToUdpConfig(m_ScePadSettings[selectedController].rumbleFromEmulatedController);
		audio.Validate();

		for (int i = 0; i < 4; i++) {
			LoadDefaultConfig(i, &m_ScePadSettings[i]);
			applySettings(i, i == (selectedController) && udp.IsActive() ? udp.GetSettings() : m_ScePadSettings[i], audio);
		}

		if (udp.SettingsFromOtherInstanceAvailable()) {
			m_ScePadSettings[selectedController] = udp.GetSettingsFromOtherInstance();
		}

		if (udp.AwaitingBringToFront()) {
			RestoreWindowFromTray();
		}

	#pragma region ImGUI + GLFW
		if (finishFrame) {
			main.Show(m_ScePadSettings, xscale);
			ImGui::Render();
			ImDrawData* drawData = ImGui::GetDrawData();
			ImGui_ImplOpenGL3_RenderDrawData(drawData);
			glfwSwapBuffers(m_GlfwWindow.get());
			occasionalFrameWhenMinimized = 0;
			ImGui::EndFrame();
		}

	#pragma endregion	

	#ifdef WINDOWS
		SetWaitableTimer(hTimer, &liDueTime, 0, NULL, NULL, 0);
		WaitForSingleObject(hTimer, INFINITE);
	#else
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	#endif
	}

#ifdef WINDOWS
	CloseHandle(hTimer);
#endif
	return true;
}

void Application::InitializeWindow(bool startHidden) {
	#ifdef LINUX
	glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	#endif
	glfwInit();

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
	if (startHidden) {
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	}

	m_GlfwWindow = std::unique_ptr<GLFWwindow, glfwDeleter>(glfwCreateWindow(1000, 720, "DualSenseY", nullptr, nullptr));

	if (!m_GlfwWindow) {
		LOGE("Failed to create windown");
		return;
	}

	glfwMakeContextCurrent(m_GlfwWindow.get());
	glfwSwapInterval(1);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		LOGE("GLAD couldn't be loaded");
	}

	#ifdef WINDOWS
	originalWndProc = (WNDPROC)SetWindowLongPtr(glfwGetWin32Window(m_GlfwWindow.get()), GWLP_WNDPROC, (LONG_PTR)CustomWndProc);
	#endif

	glfwSetWindowCloseCallback(m_GlfwWindow.get(), [](GLFWwindow* window) {
		glfwSetWindowShouldClose(window, true);
	});

	glfwSetWindowUserPointer(m_GlfwWindow.get(), this);
	glfwSetWindowIconifyCallback(m_GlfwWindow.get(), IconifyCallback);

	ImGui::CreateContext();
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(m_GlfwWindow.get(), true);          // Second param install_callback=true will install GLFW callbacks and chain to existing ones.
	ImGui_ImplOpenGL3_Init("#version 330");

	SetStyleAndColors();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // IF using Docking Branch

	#pragma region Load fonts
	ImVector<ImWchar> ranges;
	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
	builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
	builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
	builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
	static const ImWchar arabicRanges[] = { 0x0600, 0x06FF, 0 };
	builder.BuildRanges(&ranges);
	builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

	// Font index in appSettings.hpp
	ImFont* regular = io.Fonts->AddFontFromFileTTF(RESOURCES_PATH "fonts/Saira_Expanded-MediumItalic.ttf", 20, nullptr, ranges.Data);
	ImFont* japaneseAndCyrillic = io.Fonts->AddFontFromFileTTF(RESOURCES_PATH "fonts/Murecho-Regular.ttf", 20, nullptr, ranges.Data);
	ImFont* korean = io.Fonts->AddFontFromFileTTF(RESOURCES_PATH "fonts/AstaSans-Light.ttf", 20, nullptr, ranges.Data);
	ImFont* thai = io.Fonts->AddFontFromFileTTF(RESOURCES_PATH "fonts/Kanit-LightItalic.ttf", 20, nullptr, ranges.Data);
	ImFont* arabic = io.Fonts->AddFontFromFileTTF(RESOURCES_PATH "fonts/NotoSansArabic-Medium.ttf", 20, nullptr, arabicRanges);
	ImFont* chinese = io.Fonts->AddFontFromFileTTF(RESOURCES_PATH "fonts/NotoSansSC-Regular.ttf", 20, nullptr, arabicRanges);
	
	if (io.Fonts->Fonts.empty())
	{
		LOGE("Couldn't load fonts");
#ifdef WINDOWS
		MessageBox(0, "Couldn't load fonts", "Error", MB_ICONERROR);
#endif
		std::exit(0);
	}
#pragma endregion

	assert(m_GlfwWindow.get() != nullptr);
	LOGI("Window created");
}

void Application::SetStyleAndColors() {
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 10.0f;
	style.ChildRounding = 10.0f;
	style.PopupRounding = 10.0f;
	style.FrameRounding = 10.0f;
	style.GrabRounding = 10.0f;
	style.TabRounding = 10.0f;
	style.ScrollbarSize = 20.0f;

	ImVec4* colors = style.Colors;
	ImVec4 baseColor = ImVec4(0.369f, 0.075f, 0.929f, 1.0f);

#ifdef WINDOWS
	DWORD color = 0;
	BOOL opaque = FALSE;
	HRESULT hr = DwmGetColorizationColor(&color, &opaque);
	BYTE a = 0;
	BYTE r = 0;
	BYTE g = 0;
	BYTE b = 0;
	if (SUCCEEDED(hr)) {
		a = (color >> 24) & 0xFF;
		r = (color >> 16) & 0xFF;
		g = (color >> 8) & 0xFF;
		b = color & 0xFF;
		std::cout << "Accent color (ARGB): " << std::dec
			<< "A=" << (int)a << " "
			<< "R=" << (int)r << " "
			<< "G=" << (int)g << " "
			<< "B=" << (int)b << std::endl;
		baseColor = ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 255.0f);
	}
	else {
		std::cout << "Failed to get colorization color. HRESULT: " << hr << std::endl;
	}
#endif

	// Helper lambdas for color manipulation
	auto Lighten = [](const ImVec4& color, float amount) -> ImVec4 {
		return ImVec4(
			std::min(color.x + amount, 1.0f),
			std::min(color.y + amount, 1.0f),
			std::min(color.z + amount, 1.0f),
			color.w
		);
	};

	auto Darken = [](const ImVec4& color, float amount) -> ImVec4 {
		return ImVec4(
			std::max(color.x - amount, 0.0f),
			std::max(color.y - amount, 0.0f),
			std::max(color.z - amount, 0.0f),
			color.w
		);
		};

	auto AdjustAlpha = [](const ImVec4& color, float alpha) -> ImVec4 {
		return ImVec4(color.x, color.y, color.z, alpha);
	};

#ifdef WINDOWS
	isLightMode = IsWindowsLightMode();
	HWND hwnd = glfwGetWin32Window(m_GlfwWindow.get());
	EnableBlurBehind(hwnd);
	SetDarkTitleBar(hwnd, !isLightMode);
#endif
	static bool lastMode = !isLightMode;

	if(isLightMode != lastMode) {
		GLFWimage image;
		const char* whiteIcon = RESOURCES_PATH "images/iconWhite.png";
		const char* blackIcon = RESOURCES_PATH "images/icon.png";
		image.pixels = stbi_load(isLightMode ? blackIcon : whiteIcon, &image.width, &image.height, 0, 4); // RGBA
		if (image.pixels) {
			glfwSetWindowIcon(m_GlfwWindow.get(), 1, &image);
			stbi_image_free(image.pixels);
		}
	}

	colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.10f, 0.50f, 0.80f, 1.0f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.05f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.0f, 0.2f, 0.3f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.30f, 0.60f, 0.90f, 0.5f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.47f, 0.38f, 1.0f, 0.5f);

	if (isLightMode) {
		colors[ImGuiCol_WindowBg] = ImVec4(0.85f, 0.85f, 0.85f, 0.8f);
		colors[ImGuiCol_Text] = Darken(baseColor, 0.8f);
		colors[ImGuiCol_FrameBg] = Lighten(baseColor, 0.30f);
		colors[ImGuiCol_PopupBg] = Lighten(baseColor, 0.95f);
		colors[ImGuiCol_FrameBgHovered] = Lighten(baseColor, 0.10f);
		colors[ImGuiCol_SliderGrab] = Lighten(baseColor, 0.85f);
		colors[ImGuiCol_SliderGrabActive] = Lighten(baseColor, 0.10f);
		colors[ImGuiCol_Button] = Lighten(baseColor, 0.35f);
		colors[ImGuiCol_CheckMark] = Darken(colors[ImGuiCol_Button], 0.7f);
		colors[ImGuiCol_ButtonHovered] = Lighten(baseColor, 0.45f);
		colors[ImGuiCol_ButtonActive] = Lighten(baseColor, 0.85f);
	}
	else {
		#ifdef LINUX
		int r,g,b,a=0;
		colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
		#else
		colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		#endif
		colors[ImGuiCol_Text] = Lighten(baseColor, 1.0f);
		colors[ImGuiCol_FrameBg] = Lighten(baseColor, 0.30f);
		colors[ImGuiCol_PopupBg] = Darken(baseColor, 0.95f);
		colors[ImGuiCol_FrameBgHovered] = Lighten(baseColor, 0.10f);
		colors[ImGuiCol_SliderGrab] = (r < 40 || g < 40 || b < 40) ? Lighten(baseColor, 0.75f) : Darken(baseColor, 0.75f);
		colors[ImGuiCol_SliderGrab] = (r > 100 || g > 100 || b > 100) ? Darken(baseColor, 0.75f) : Lighten(baseColor, 0.75f);
		colors[ImGuiCol_SliderGrabActive] = Lighten(baseColor, 0.10f);
		colors[ImGuiCol_Button] = (r < 40 || g < 40 || b < 40) ? Lighten(baseColor, 0.15f) : Darken(baseColor, 0.15f);
		colors[ImGuiCol_CheckMark] = (r < 40 || g < 40 || b < 40) ? Lighten(colors[ImGuiCol_Button], 0.7f) : Darken(colors[ImGuiCol_Button], 0.7f);
		colors[ImGuiCol_Button] = (r > 100 || g > 100 || b > 100) ? Darken(baseColor, 0.15f) : Lighten(baseColor, 0.15f);
		colors[ImGuiCol_CheckMark] = (r > 100 || g > 100 || b > 100) ? Darken(colors[ImGuiCol_Button], 0.7f) : Lighten(colors[ImGuiCol_Button], 0.7f);
		colors[ImGuiCol_ButtonHovered] = Lighten(baseColor, 0.55f);
		colors[ImGuiCol_ButtonActive] = Lighten(baseColor, 1.00f);
	}

	lastMode = isLightMode;
}

void Application::SetupTray() {
	m_Tray = std::make_unique<Tray::Tray>("DualSenseY", RESOURCES_PATH "images/icon.ico");
#if defined(_WIN32)
	m_BatteryIconRenderer.Init(RESOURCES_PATH "images/battery/");
#endif
	m_Tray->setOnClick([this] {
		RestoreWindowFromTray();
	});
	m_Tray->addEntry(Tray::Button("Show window", [&] {
		RestoreWindowFromTray();
	}));

	m_Tray->addEntry(Tray::Button("Hide to tray", [&] {
		HideWindowToTray();
	}));

	m_Tray->addEntry(Tray::Separator());

	m_Tray->addEntry(Tray::Button("Exit", [&] {
		m_Tray->exit();
		glfwSetWindowShouldClose(m_GlfwWindow.get(), GLFW_TRUE);
	}));
	
	m_TrayThread = std::thread([this] {
		m_Tray->run();
	});
	m_TrayThread.detach();
}

#if defined(_WIN32)
void Application::UpdateTrayBatteryStatus(int controllerIndex, Strings& strings) {
	if (!m_Tray) return;

	auto now = std::chrono::steady_clock::now();
	auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastTrayUpdateTime).count();

	s_ScePadData padState = {};
	bool connected = (controllerIndex >= 0 && controllerIndex < 4 &&
	                  scePadReadState(g_ScePad[controllerIndex], &padState) == SCE_OK &&
	                  padState.connected);

	uint8_t batteryLevel = connected ? padState.batteryLevel : 0;
	bool isCharging = connected ? padState.isCharging : false;

	// Track connection state & grace period per controller
	if (controllerIndex >= 0 && controllerIndex < 4) {
		if (connected && !m_WasConnected[controllerIndex]) {
			m_WasConnected[controllerIndex] = true;
			m_ConnectionStartTime[controllerIndex] = now;
			m_ZeroBatteryStartTime[controllerIndex] = (batteryLevel == 0) ? now : std::chrono::steady_clock::time_point{};
		} else if (!connected && m_WasConnected[controllerIndex]) {
			m_WasConnected[controllerIndex] = false;
			m_HasNotifiedLowBattery[controllerIndex] = false;
			m_ZeroBatteryStartTime[controllerIndex] = {};
			m_ConnectionStartTime[controllerIndex] = {};
		}
	}

	// Low battery notification check
	if (connected && controllerIndex >= 0 && controllerIndex < 4) {
		s_scePadSettings& settings = m_ScePadSettings[controllerIndex];

		// Glitch filter: Track duration of 0% battery reading
		if (batteryLevel == 0) {
			if (m_ZeroBatteryStartTime[controllerIndex] == std::chrono::steady_clock::time_point{}) {
				m_ZeroBatteryStartTime[controllerIndex] = now;
			}
		} else {
			m_ZeroBatteryStartTime[controllerIndex] = {};
		}

		auto connectionDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - m_ConnectionStartTime[controllerIndex]
		).count();

		bool isStableZero = false;
		if (batteryLevel == 0 && m_ZeroBatteryStartTime[controllerIndex] != std::chrono::steady_clock::time_point{}) {
			auto zeroDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				now - m_ZeroBatteryStartTime[controllerIndex]
			).count();
			isStableZero = (zeroDurationMs >= 3000);
		}

		// 1. Grace period: at least 3000 ms connected
		// 2. Glitch filter: batteryLevel > 0 OR continuous 0% for >= 3000 ms
		bool batteryValid = (connectionDurationMs >= 3000) && (batteryLevel > 0 || isStableZero);

		if (settings.lowBatteryNotification && !isCharging && batteryValid && batteryLevel <= (uint8_t)settings.lowBatteryThreshold) {
			if (!m_HasNotifiedLowBattery[controllerIndex]) {
				m_HasNotifiedLowBattery[controllerIndex] = true;
				std::string title = strings.GetString("LowBatteryWarningTitle");
				char msgBuf[256];
				snprintf(msgBuf, sizeof(msgBuf), strings.GetString("LowBatteryWarningMsg").c_str(), (int)batteryLevel);
				m_Tray->showNotification(title, msgBuf);
			}
		} else if (isCharging || batteryLevel > (uint8_t)std::min(100, settings.lowBatteryThreshold + 3)) {
			// 3. Hysteresis: Reset flag only when charging or battery rises clearly above threshold
			m_HasNotifiedLowBattery[controllerIndex] = false;
		}
	} else if (controllerIndex >= 0 && controllerIndex < 4) {
		m_HasNotifiedLowBattery[controllerIndex] = false;
	}

	// Update tray icon & tooltip
	bool isLightTheme = BatteryIconRenderer::IsSystemLightTheme();
	int iconSize = GetSystemMetrics(SM_CXSMICON);
	if (iconSize <= 0) iconSize = 16;

	int batteryBracket = BatteryIconRenderer::GetBatteryBracket(batteryLevel);

	bool iconChanged = (connected != m_LastTrayConnected ||
	                    isCharging != m_LastTrayCharging ||
	                    batteryBracket != m_LastTrayBatteryBracket ||
	                    isLightTheme != m_LastTrayLightTheme ||
	                    iconSize != m_LastTrayIconSize ||
	                    m_CurrentTrayIcon == nullptr);

	bool tooltipChanged = (connected != m_LastTrayConnected ||
	                       batteryLevel != m_LastTrayBatteryLevel ||
	                       isCharging != m_LastTrayCharging);

	bool timeToUpdate = (elapsedMs >= 1500);

	if (iconChanged) {
		m_LastTrayUpdateTime = now;
		m_LastTrayConnected = connected;
		m_LastTrayBatteryLevel = batteryLevel;
		m_LastTrayBatteryBracket = batteryBracket;
		m_LastTrayCharging = isCharging;
		m_LastTrayLightTheme = isLightTheme;
		m_LastTrayIconSize = iconSize;

		HICON newIcon = m_BatteryIconRenderer.GenerateBatteryIcon(connected, batteryLevel, isCharging, iconSize);
		if (newIcon) {
			char tooltip[128];
			if (connected) {
				if (isCharging) {
					snprintf(tooltip, sizeof(tooltip), "DualSenseY: %d%% [%s]", (int)batteryLevel, strings.GetString("Charging").c_str());
				} else {
					snprintf(tooltip, sizeof(tooltip), "DualSenseY: %d%%", (int)batteryLevel);
				}
			} else {
				snprintf(tooltip, sizeof(tooltip), "DualSenseY (%s)", strings.GetString("Disconnected").c_str());
			}

			m_Tray->updateTrayIcon(newIcon, tooltip);

			if (m_CurrentTrayIcon) {
				DestroyIcon(m_CurrentTrayIcon);
			}
			m_CurrentTrayIcon = newIcon;
		}
	} else if (tooltipChanged && timeToUpdate) {
		m_LastTrayUpdateTime = now;
		m_LastTrayBatteryLevel = batteryLevel;

		char tooltip[128];
		if (connected) {
			if (isCharging) {
				snprintf(tooltip, sizeof(tooltip), "DualSenseY: %d%% [%s]", (int)batteryLevel, strings.GetString("Charging").c_str());
			} else {
				snprintf(tooltip, sizeof(tooltip), "DualSenseY: %d%%", (int)batteryLevel);
			}
		} else {
			snprintf(tooltip, sizeof(tooltip), "DualSenseY (%s)", strings.GetString("Disconnected").c_str());
		}

		m_Tray->updateTrayIcon(nullptr, tooltip);
	}
}
#endif

void Application::HideWindowToTray() {
	glfwHideWindow(m_GlfwWindow.get());
}

void Application::RestoreWindowFromTray() {
	glfwShowWindow(m_GlfwWindow.get());
	glfwRestoreWindow(m_GlfwWindow.get());
	glfwFocusWindow(m_GlfwWindow.get());
#ifdef WINDOWS
	HWND hwnd = glfwGetWin32Window(m_GlfwWindow.get());
	if (hwnd) {
		ShowWindow(hwnd, SW_RESTORE);
		SetForegroundWindow(hwnd);
		BringWindowToTop(hwnd);
	}
#endif
}

Application::~Application() {
	// Hide the window immediately to prevent user from interacting with it while it's closing
	glfwHideWindow(m_GlfwWindow.get());

	// Unhide controllers so devices are not left cloaked after closing
#ifdef WINDOWS
	for (int i = 0; i < 4; i++) {
		std::string path = scePadGetPath(g_ScePad[i]);
		if (!path.empty()) {
			UnhideController(path);
		}
	}
	DisableHidHideCloak();

	if (m_AppSettings.DisableAllBluetoothControllersOnExit) {
		for (int i = 0; i < 4; i++)
			DisableBluetoothDevice(scePadGetMacAddress(g_ScePad[i]));
	}
#endif

	m_Tray->exit();
	if(m_TrayThread.joinable())
		m_TrayThread.join();

#if defined(_WIN32)
	if (m_CurrentTrayIcon) {
		DestroyIcon(m_CurrentTrayIcon);
		m_CurrentTrayIcon = nullptr;
	}
#endif

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	scePadTerminate();
}