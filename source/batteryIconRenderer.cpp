#include "batteryIconRenderer.hpp"
#include <fstream>
#include <vector>
#include <algorithm>
#include <regex>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d2d1_3.h>
#include <d2d1_3helper.h>
#include <d2d1svg.h>
#include <shlwapi.h>

#if defined(min)
#undef min
#endif
#if defined(max)
#undef max
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")

struct BatteryIconRenderer::D2DState {
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    ID2D1DeviceContext5* d2dContext5 = nullptr;

    ~D2DState() {
        if (d2dContext5) { d2dContext5->Release(); d2dContext5 = nullptr; }
        if (d3dContext) { d3dContext->Release(); d3dContext = nullptr; }
        if (d3dDevice) { d3dDevice->Release(); d3dDevice = nullptr; }
    }
};
#endif

BatteryIconRenderer::BatteryIconRenderer() {
#if defined(_WIN32)
    m_d2d = new D2DState();
#endif
}

BatteryIconRenderer::~BatteryIconRenderer() {
#if defined(_WIN32)
    CleanupD2D();
    delete m_d2d;
    m_d2d = nullptr;
#endif
}

bool BatteryIconRenderer::IsSystemLightTheme() {
#if defined(_WIN32)
    DWORD value = 0;
    DWORD size = sizeof(value);
    LONG res = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size
    );
    if (res == ERROR_SUCCESS) {
        return value != 0;
    }
#endif
    return false;
}

int BatteryIconRenderer::GetBatteryBracket(uint8_t level) {
    if (level <= 15) return 0;
    if (level <= 35) return 1;
    if (level <= 60) return 2;
    if (level <= 85) return 3;
    return 4;
}

bool BatteryIconRenderer::Init(const std::string& batteryIconsDir) {
    m_batteryIconsDir = batteryIconsDir;
    if (!m_batteryIconsDir.empty() && m_batteryIconsDir.back() != '/' && m_batteryIconsDir.back() != '\\') {
        m_batteryIconsDir += '/';
    }
#if defined(_WIN32)
    InitD2D();
#endif
    return true;
}

#if defined(_WIN32)
bool BatteryIconRenderer::InitD2D() {
    if (!m_d2d) {
        m_d2d = new D2DState();
    }
    if (m_d2d->d2dContext5 && m_d2d->d3dDevice) {
        if (m_d2d->d3dDevice->GetDeviceRemovedReason() == S_OK) {
            return true;
        }
        CleanupD2D();
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_1
    };
    D3D_FEATURE_LEVEL featureLevel;

    // Use WARP software rasterizer for fast, reliable, crash-free offscreen rendering
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_d2d->d3dDevice,
        &featureLevel,
        &m_d2d->d3dContext
    );
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels, ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &m_d2d->d3dDevice,
            &featureLevel,
            &m_d2d->d3dContext
        );
    }
    if (FAILED(hr) || !m_d2d->d3dDevice) return false;

    IDXGIDevice* dxgiDevice = nullptr;
    hr = m_d2d->d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr) || !dxgiDevice) return false;

    ID2D1Factory3* d2dFactory = nullptr;
    D2D1_FACTORY_OPTIONS options = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &options, (void**)&d2dFactory);
    if (FAILED(hr) || !d2dFactory) {
        dxgiDevice->Release();
        return false;
    }

    ID2D1Device2* d2dDevice = nullptr;
    hr = d2dFactory->CreateDevice(dxgiDevice, &d2dDevice);
    dxgiDevice->Release();
    d2dFactory->Release();
    if (FAILED(hr) || !d2dDevice) return false;

    ID2D1DeviceContext* baseContext = nullptr;
    hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &baseContext);
    d2dDevice->Release();
    if (FAILED(hr) || !baseContext) return false;

    hr = baseContext->QueryInterface(IID_PPV_ARGS(&m_d2d->d2dContext5));
    baseContext->Release();

    return SUCCEEDED(hr) && (m_d2d->d2dContext5 != nullptr);
}

void BatteryIconRenderer::CleanupD2D() {
    if (m_d2d) {
        if (m_d2d->d2dContext5) { m_d2d->d2dContext5->Release(); m_d2d->d2dContext5 = nullptr; }
        if (m_d2d->d3dContext) { m_d2d->d3dContext->Release(); m_d2d->d3dContext = nullptr; }
        if (m_d2d->d3dDevice) { m_d2d->d3dDevice->Release(); m_d2d->d3dDevice = nullptr; }
    }
}

static std::string TrimSvgString(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string BatteryIconRenderer::PreprocessSvg(const std::string& svgInput) {
    std::string svg = svgInput;

    // 1. Extract CSS rules inside <style>...</style> and inline them as presentation attributes
    std::regex styleRegex(R"(<style[^>]*>([\s\S]*?)<\/style>)", std::regex::icase);
    std::smatch styleMatch;

    std::unordered_map<std::string, std::string> classStyles;

    std::string tempSvg = svg;
    while (std::regex_search(tempSvg, styleMatch, styleRegex)) {
        std::string styleContent = styleMatch[1].str();

        std::regex ruleRegex(R"(\.([a-zA-Z0-9_-]+)\s*\{([^}]+)\})");
        std::smatch ruleMatch;
        std::string tempRules = styleContent;
        while (std::regex_search(tempRules, ruleMatch, ruleRegex)) {
            std::string className = ruleMatch[1].str();
            std::string decls = ruleMatch[2].str();

            // Convert CSS declarations "prop:val; prop2:val2;" into XML attributes 'prop="val" prop2="val2"'
            std::string xmlAttrs;
            std::stringstream ss(decls);
            std::string item;
            while (std::getline(ss, item, ';')) {
                item = TrimSvgString(item);
                if (item.empty()) continue;
                size_t colon = item.find(':');
                if (colon != std::string::npos) {
                    std::string prop = TrimSvgString(item.substr(0, colon));
                    std::string val = TrimSvgString(item.substr(colon + 1));
                    if (!prop.empty() && !val.empty()) {
                        if (!xmlAttrs.empty()) xmlAttrs += " ";
                        xmlAttrs += prop + "=\"" + val + "\"";
                    }
                }
            }
            if (!xmlAttrs.empty()) {
                classStyles[className] = xmlAttrs;
            }
            tempRules = ruleMatch.suffix().str();
        }
        tempSvg = styleMatch.suffix().str();
    }

    // Replace class="className" with inline XML attributes
    for (const auto& [cls, attrs] : classStyles) {
        std::regex classAttrRegex(R"(class\s*=\s*["']\s*)" + cls + R"(\s*["'])");
        svg = std::regex_replace(svg, classAttrRegex, attrs);
    }

    // Remove <style> blocks
    svg = std::regex_replace(svg, styleRegex, "");

    // Remove empty <defs></defs>
    std::regex defsRegex(R"(<defs[^>]*>\s*<\/defs\s*>)");
    svg = std::regex_replace(svg, defsRegex, "");

    // Fallback: If known classes like cls-1 or cls-2 are still present without style block
    std::regex cls1Regex(R"(class\s*=\s*["']cls-1["'])");
    svg = std::regex_replace(svg, cls1Regex, "fill=\"none\" stroke=\"#fff\" stroke-linejoin=\"round\" stroke-width=\"2px\"");
    std::regex cls2Regex(R"(class\s*=\s*["']cls-2["'])");
    svg = std::regex_replace(svg, cls2Regex, "fill=\"#0f7c11\"");
    std::regex cls3Regex(R"(class\s*=\s*["']cls-3["'])");
    svg = std::regex_replace(svg, cls3Regex, "fill=\"#fff\"");

    // Ensure any graphic element with stroke has fill="none" if fill is missing
    // In SVG spec, default fill is black (#000000). If stroke is specified but fill is omitted,
    // Direct2D / SVG spec fills the shape with solid black!
    std::regex tagRegex(R"(<((?:path|polygon|polyline|circle|rect|ellipse)\b[^>]*)(/?>))", std::regex::icase);
    std::string result;
    auto words_begin = std::sregex_iterator(svg.begin(), svg.end(), tagRegex);
    auto words_end = std::sregex_iterator();

    size_t lastPos = 0;
    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        result.append(svg, lastPos, match.position() - lastPos);

        std::string tagContent = match[1].str();
        std::string tagEnd = match[2].str();

        bool hasFill = (tagContent.find("fill=") != std::string::npos || tagContent.find("fill =") != std::string::npos);
        bool hasStroke = (tagContent.find("stroke=") != std::string::npos || tagContent.find("stroke =") != std::string::npos);

        if (!hasFill && hasStroke) {
            tagContent += " fill=\"none\"";
        }

        result += "<" + tagContent + tagEnd;
        lastPos = match.position() + match.length();
    }
    result.append(svg, lastPos, svg.length() - lastPos);

    return result;
}

std::string BatteryIconRenderer::SetSvgStrokeColor(std::string svg, const std::string& color) {
    // 1. If stroke: exists in CSS style, replace color
    std::regex strokeCssRegex(R"(stroke\s*:\s*#[0-9a-fA-F]{3,6})");
    if (std::regex_search(svg, strokeCssRegex)) {
        svg = std::regex_replace(svg, strokeCssRegex, "stroke:" + color);
    }

    // 2. Preprocess to convert CSS to XML presentation attributes
    svg = PreprocessSvg(svg);

    // 3. Replace stroke="..." attributes with new color
    std::regex strokeAttrRegex(R"(stroke\s*=\s*["'][^"']*["'])");
    if (std::regex_search(svg, strokeAttrRegex)) {
        svg = std::regex_replace(svg, strokeAttrRegex, "stroke=\"" + color + "\"");
    } else {
        std::regex pathRegex(R"(<path\b)", std::regex::icase);
        svg = std::regex_replace(svg, pathRegex, "<path stroke=\"" + color + "\"");
    }

    return svg;
}

std::string BatteryIconRenderer::SetSvgFillColor(std::string svg, const std::string& color) {
    // 1. If fill: exists in CSS style, replace color
    std::regex fillCssRegex(R"(fill\s*:\s*#[0-9a-fA-F]{3,6})");
    if (std::regex_search(svg, fillCssRegex)) {
        svg = std::regex_replace(svg, fillCssRegex, "fill:" + color);
    }

    // 2. Preprocess to convert CSS to XML presentation attributes
    svg = PreprocessSvg(svg);

    // 3. Replace or inject fill attribute
    std::regex fillAttrRegex(R"(fill\s*=\s*["'][^"']*["'])");
    if (std::regex_search(svg, fillAttrRegex)) {
        svg = std::regex_replace(svg, fillAttrRegex, "fill=\"" + color + "\"");
    } else {
        std::regex polyRegex(R"(<(polygon|path|rect|circle)\b)", std::regex::icase);
        svg = std::regex_replace(svg, polyRegex, "<$1 fill=\"" + color + "\"");
    }

    return svg;
}

std::string BatteryIconRenderer::SetSvgBoltColors(std::string svg, const std::string& fillColor, const std::string& strokeColor, float strokeWidth) {
    // 1. Remove <style>...</style> and <defs>...</defs>
    std::regex styleRegex(R"(<style[^>]*>[\s\S]*?<\/style>)", std::regex::icase);
    svg = std::regex_replace(svg, styleRegex, "");
    std::regex defsRegex(R"(<defs[^>]*>\s*<\/defs\s*>)", std::regex::icase);
    svg = std::regex_replace(svg, defsRegex, "");

    char strokeWidthBuf[32];
    snprintf(strokeWidthBuf, sizeof(strokeWidthBuf), "%.1fpx", strokeWidth);

    // 2. Format <polygon>: inner bolt with fillColor and strokeColor outline
    std::regex polyRegex(R"(<polygon\b([^>]*?)(/?)>)");
    std::smatch polyMatch;
    if (std::regex_search(svg, polyMatch, polyRegex)) {
        std::string polyAttrs = polyMatch[1].str();
        polyAttrs = std::regex_replace(polyAttrs, std::regex(R"(class\s*=\s*["'][^"']*["'])"), "");
        polyAttrs = std::regex_replace(polyAttrs, std::regex(R"(fill\s*=\s*["'][^"']*["'])"), "");
        polyAttrs = std::regex_replace(polyAttrs, std::regex(R"(stroke\s*=\s*["'][^"']*["'])"), "");
        polyAttrs = std::regex_replace(polyAttrs, std::regex(R"(stroke-width\s*=\s*["'][^"']*["'])"), "");
        polyAttrs = std::regex_replace(polyAttrs, std::regex(R"(stroke-linejoin\s*=\s*["'][^"']*["'])"), "");
        size_t last = polyAttrs.find_last_not_of(" \t\r\n/");
        if (last != std::string::npos) {
            polyAttrs = polyAttrs.substr(0, last + 1);
        } else {
            polyAttrs.clear();
        }

        std::string newPoly = "<polygon fill=\"" + fillColor + "\" stroke=\"" + strokeColor + 
                              "\" stroke-width=\"" + strokeWidthBuf + "\" stroke-linejoin=\"round\" " + polyAttrs + "/>";
        svg = std::regex_replace(svg, polyRegex, newPoly);
    }

    // 3. Format <path>: outer border ring with strokeColor fill and stroke
    std::regex pathRegex(R"(<path\b([^>]*?)(/?)>)");
    std::smatch pathMatch;
    if (std::regex_search(svg, pathMatch, pathRegex)) {
        std::string pathAttrs = pathMatch[1].str();
        pathAttrs = std::regex_replace(pathAttrs, std::regex(R"(class\s*=\s*["'][^"']*["'])"), "");
        pathAttrs = std::regex_replace(pathAttrs, std::regex(R"(fill\s*=\s*["'][^"']*["'])"), "");
        pathAttrs = std::regex_replace(pathAttrs, std::regex(R"(stroke\s*=\s*["'][^"']*["'])"), "");
        pathAttrs = std::regex_replace(pathAttrs, std::regex(R"(stroke-width\s*=\s*["'][^"']*["'])"), "");
        pathAttrs = std::regex_replace(pathAttrs, std::regex(R"(stroke-linejoin\s*=\s*["'][^"']*["'])"), "");
        size_t last = pathAttrs.find_last_not_of(" \t\r\n/");
        if (last != std::string::npos) {
            pathAttrs = pathAttrs.substr(0, last + 1);
        } else {
            pathAttrs.clear();
        }

        std::string newPath = "<path fill=\"" + strokeColor + "\" stroke=\"" + strokeColor + 
                              "\" stroke-width=\"0.5px\" stroke-linejoin=\"round\" " + pathAttrs + "/>";
        svg = std::regex_replace(svg, pathRegex, newPath);
    }

    return svg;
}

std::string BatteryIconRenderer::LoadSvg(const std::string& filename) {
    auto it = m_svgCache.find(filename);
    if (it != m_svgCache.end()) {
        return it->second;
    }

    std::vector<std::string> candidatePaths = {
        m_batteryIconsDir + filename,
        std::string(RESOURCES_PATH) + "images/battery/" + filename,
        std::string("./resources/images/battery/") + filename,
        std::string("resources/images/battery/") + filename
    };

    for (const auto& path : candidatePaths) {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (!content.empty()) {
                std::string processed = PreprocessSvg(content);
                m_svgCache[filename] = processed;
                return processed;
            }
        }
    }
    return "";
}

bool BatteryIconRenderer::DrawSvgString(const std::string& svgContent, int iconSize) {
    if (svgContent.empty() || iconSize <= 0) return false;
    if (!m_d2d || !m_d2d->d2dContext5) return false;

    IStream* stream = SHCreateMemStream((const BYTE*)svgContent.data(), (UINT)svgContent.size());
    if (!stream) return false;

    ID2D1SvgDocument* svgDoc = nullptr;
    HRESULT hr = m_d2d->d2dContext5->CreateSvgDocument(stream, D2D1::SizeF((float)iconSize, (float)iconSize), &svgDoc);
    if (SUCCEEDED(hr) && svgDoc) {
        m_d2d->d2dContext5->DrawSvgDocument(svgDoc);
        svgDoc->Release();
    }
    stream->Release();
    return SUCCEEDED(hr);
}

HICON BatteryIconRenderer::CreateHIconFromTexture(ID3D11Texture2D* renderTargetTex, int iconSize) {
    if (!renderTargetTex || iconSize <= 0 || !m_d2d) return nullptr;

    D3D11_TEXTURE2D_DESC stageDesc = {};
    renderTargetTex->GetDesc(&stageDesc);
    stageDesc.Usage = D3D11_USAGE_STAGING;
    stageDesc.BindFlags = 0;
    stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* stagingTex = nullptr;
    HRESULT hr = m_d2d->d3dDevice->CreateTexture2D(&stageDesc, nullptr, &stagingTex);
    if (FAILED(hr) || !stagingTex) {
        renderTargetTex->Release();
        return nullptr;
    }

    m_d2d->d3dContext->CopyResource(stagingTex, renderTargetTex);
    renderTargetTex->Release();

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = m_d2d->d3dContext->Map(stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        stagingTex->Release();
        return nullptr;
    }

    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = iconSize;
    bi.bV5Height = -iconSize; // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(nullptr);
    void* pBits = nullptr;
    HBITMAP hColor = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);

    HICON hIcon = nullptr;
    if (hColor && pBits) {
        uint8_t* dst = (uint8_t*)pBits;
        for (int y = 0; y < iconSize; y++) {
            const uint8_t* srcRow = ((const uint8_t*)mapped.pData) + y * mapped.RowPitch;
            uint8_t* dstRow = dst + y * (iconSize * 4);
            memcpy(dstRow, srcRow, iconSize * 4);
        }

        int maskPitch = ((iconSize + 15) / 16) * 2;
        std::vector<uint8_t> maskBits(maskPitch * iconSize, 0);
        HBITMAP hMask = CreateBitmap(iconSize, iconSize, 1, 1, maskBits.data());

        ICONINFO ii = {};
        ii.fIcon = TRUE;
        ii.hbmMask = hMask;
        ii.hbmColor = hColor;
        hIcon = CreateIconIndirect(&ii);

        DeleteObject(hColor);
        DeleteObject(hMask);
    }

    m_d2d->d3dContext->Unmap(stagingTex, 0);
    stagingTex->Release();

    return hIcon;
}

HICON BatteryIconRenderer::RenderSvgToHIcon(const std::string& svgContent, int iconSize) {
    if (svgContent.empty() || iconSize <= 0) return nullptr;
    if (!InitD2D()) return nullptr;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = (UINT)iconSize;
    texDesc.Height = (UINT)iconSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D* renderTargetTex = nullptr;
    HRESULT hr = m_d2d->d3dDevice->CreateTexture2D(&texDesc, nullptr, &renderTargetTex);
    if (FAILED(hr) || !renderTargetTex) return nullptr;

    IDXGISurface* surface = nullptr;
    hr = renderTargetTex->QueryInterface(IID_PPV_ARGS(&surface));
    if (FAILED(hr) || !surface) {
        renderTargetTex->Release();
        return nullptr;
    }

    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    ID2D1Bitmap1* targetBitmap = nullptr;
    hr = m_d2d->d2dContext5->CreateBitmapFromDxgiSurface(surface, &bp, &targetBitmap);
    surface->Release();
    if (FAILED(hr) || !targetBitmap) {
        renderTargetTex->Release();
        return nullptr;
    }

    m_d2d->d2dContext5->SetTarget(targetBitmap);
    m_d2d->d2dContext5->BeginDraw();
    m_d2d->d2dContext5->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    DrawSvgString(svgContent, iconSize);

    hr = m_d2d->d2dContext5->EndDraw();
    m_d2d->d2dContext5->SetTarget(nullptr);
    targetBitmap->Release();

    if (FAILED(hr)) {
        renderTargetTex->Release();
        return nullptr;
    }

    return CreateHIconFromTexture(renderTargetTex, iconSize);
}

HICON BatteryIconRenderer::GenerateBatteryIcon(bool connected, uint8_t batteryLevel, bool isCharging, int iconSize) {
    if (iconSize <= 0) {
        iconSize = GetSystemMetrics(SM_CXSMICON);
        if (iconSize <= 0) iconSize = 16;
    }

    if (!InitD2D()) return nullptr;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = (UINT)iconSize;
    texDesc.Height = (UINT)iconSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D* renderTargetTex = nullptr;
    HRESULT hr = m_d2d->d3dDevice->CreateTexture2D(&texDesc, nullptr, &renderTargetTex);
    if (FAILED(hr) || !renderTargetTex) return nullptr;

    IDXGISurface* surface = nullptr;
    hr = renderTargetTex->QueryInterface(IID_PPV_ARGS(&surface));
    if (FAILED(hr) || !surface) {
        renderTargetTex->Release();
        return nullptr;
    }

    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    ID2D1Bitmap1* targetBitmap = nullptr;
    hr = m_d2d->d2dContext5->CreateBitmapFromDxgiSurface(surface, &bp, &targetBitmap);
    surface->Release();
    if (FAILED(hr) || !targetBitmap) {
        renderTargetTex->Release();
        return nullptr;
    }

    m_d2d->d2dContext5->SetTarget(targetBitmap);
    m_d2d->d2dContext5->BeginDraw();

    // 1. Wyczyść tło na pełną przezroczystość
    m_d2d->d2dContext5->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // 2. WARSTWA 1 (Spód): Wypełnienie poziomu baterii
    if (connected) {
        std::string fillFile;
        if (batteryLevel <= 15) {
            fillFile = "pad_10-01.svg";
        } else if (batteryLevel <= 35) {
            fillFile = "pad_25-01.svg";
        } else if (batteryLevel <= 60) {
            fillFile = "pad_50-01.svg";
        } else if (batteryLevel <= 85) {
            fillFile = "pad_75-01.svg";
        } else {
            fillFile = "pad_100-01.svg";
        }
        std::string fillSvg = LoadSvg(fillFile);
        if (!fillSvg.empty()) {
            DrawSvgString(fillSvg, iconSize);
        }
    }

    // 3. WARSTWA 2 (Środek): Obrys pada (pad_outline-01.svg) z dynamicznym kolorem
    {
        bool isLightTheme = IsSystemLightTheme();
        std::string outlineColor = isLightTheme ? "#000000" : "#FFFFFF";

        std::string outlineSvg = LoadSvg("pad_outline-01.svg");
        if (!outlineSvg.empty()) {
            outlineSvg = SetSvgStrokeColor(outlineSvg, outlineColor);
            DrawSvgString(outlineSvg, iconSize);
        }
    }

    // 4. WARSTWA 3 (Wierzch): Piorun ładowania (pad_bolt-01.svg) z dwukolorowym obrysem
    if (connected && isCharging) {
        bool isLightTheme = IsSystemLightTheme();
        std::string boltFill = isLightTheme ? "#000000" : "#FFFFFF";
        std::string boltStroke = isLightTheme ? "#FFFFFF" : "#000000";

        std::string boltSvg = LoadSvg("pad_bolt-01.svg");
        if (!boltSvg.empty()) {
            boltSvg = SetSvgBoltColors(boltSvg, boltFill, boltStroke, 1.5f);
            DrawSvgString(boltSvg, iconSize);
        }
    }

    hr = m_d2d->d2dContext5->EndDraw();
    m_d2d->d2dContext5->SetTarget(nullptr);
    targetBitmap->Release();

    if (FAILED(hr)) {
        renderTargetTex->Release();
        return nullptr;
    }

    return CreateHIconFromTexture(renderTargetTex, iconSize);
}
#endif
