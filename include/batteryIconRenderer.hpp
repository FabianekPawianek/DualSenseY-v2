#ifndef BATTERY_ICON_RENDERER_HPP
#define BATTERY_ICON_RENDERER_HPP

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#if defined(min)
#undef min
#endif
#if defined(max)
#undef max
#endif
#endif

#include <string>
#include <cstdint>
#include <unordered_map>

class BatteryIconRenderer {
public:
    BatteryIconRenderer();
    ~BatteryIconRenderer();

    static bool IsSystemLightTheme();
    static int GetBatteryBracket(uint8_t level);
    static std::string PreprocessSvg(const std::string& svg);
    static std::string SetSvgStrokeColor(std::string svg, const std::string& color);
    static std::string SetSvgFillColor(std::string svg, const std::string& color);

    bool Init(const std::string& batteryIconsDir = "");

#if defined(_WIN32)
    HICON GenerateBatteryIcon(bool connected, uint8_t batteryLevel, bool isCharging, int iconSize = 0);
#endif

private:
    std::string m_batteryIconsDir;
    std::unordered_map<std::string, std::string> m_svgCache;

#if defined(_WIN32)
    struct D2DState;
    D2DState* m_d2d = nullptr;

    bool InitD2D();
    void CleanupD2D();
    std::string LoadSvg(const std::string& filename);
    bool DrawSvgString(const std::string& svgContent, int iconSize);
    HICON CreateHIconFromTexture(struct ID3D11Texture2D* renderTargetTex, int iconSize);
    HICON RenderSvgToHIcon(const std::string& svgContent, int iconSize);
#endif
};

#endif // BATTERY_ICON_RENDERER_HPP
