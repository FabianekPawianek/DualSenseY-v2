#include "appSettings.hpp"
#include "utils.hpp"
#include <platform_folders.h>
#include <filesystem>
#include <fstream>
#include "log.hpp"

static inline std::filesystem::path directory = std::filesystem::path(sago::getDocumentsFolder() + "/DSY/");
static inline std::filesystem::path filePath = directory / "Config.json";

void SaveAppSettings(AppSettings* appSettings) {
	nlohmann::json j = *appSettings;

	if (!std::filesystem::is_directory(directory))
		std::filesystem::create_directories(directory);

	std::ofstream(filePath) << j.dump(4);
}

void LoadAppSettings(AppSettings* appSettings) {
	try {
		if (std::filesystem::exists(filePath)) {
			std::ifstream ifs(filePath);
			if (!ifs) return;
			nlohmann::json j;
			ifs >> j;
			*appSettings = j.get<AppSettings>();
		}
	}
	catch (...) {
		LOGE("Failed to load application config file");
	}

#if defined(WINDOWS) || defined(_WIN32)
	bool regEnabled = false;
	bool regDelayEnabled = false;
	int regDelaySeconds = 15;
	bool regStartAsAdmin = true;
	if (GetAutostartWindows(regEnabled, regDelayEnabled, regDelaySeconds, regStartAsAdmin)) {
		appSettings->startWithWindows = regEnabled;
		if (regEnabled) {
			appSettings->startupDelayEnabled = regDelayEnabled;
			appSettings->startupDelaySeconds = regDelaySeconds;
			appSettings->startAsAdmin = regStartAsAdmin;
		}
	} else {
		appSettings->startWithWindows = false;
	}
#endif
}