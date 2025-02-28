#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE

#include <WinSDKVer.h>

#define _WIN32_WINNT _WIN32_WINNT_WIN10

#include <sdkddkver.h>
#include <windows.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <ShellScalingApi.h>
#include <sstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <cassert>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <winuser.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
#define ARRAY_NUM(arr) (sizeof(arr) / sizeof(arr[0]))

char shaders[] = STRINGIFY(
    struct VS_Input
    {
        float2 pos : POSITION;
        float3 color : COL;
        float quant : COL1;
    };

    struct VS_Output
    {
        float4 color : COL;
        float quant : COL1;
        float4 position : SV_POSITION;
    };

    VS_Output vs_main(float2 pos : POSITION, float3 color : COL, float quant : COL1)
    {
        VS_Output output;
        output.position = float4(pos.xy, 0.0f, 1.0f);
        output.color = float4(color.xyz, 1);
        output.quant = quant;

        return output;
    }

    float4 ps_main(float4 color : COL, nointerpolation float quant: COL1) : SV_TARGET
    {
        if (quant != 0) {
            return floor(color / quant) * quant;
        }
        return color;
    }
);

static bool global_windowDidResize = false;

LONG g_wndStyle = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
RECT g_wndRect;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    LRESULT result = 0;
    if (msg == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
        SetCursor(NULL);

        return TRUE;
    }
    switch (msg) {
        case WM_KEYDOWN: {
            if (wparam == VK_ESCAPE) {
                DestroyWindow(hwnd);
            }
            break;
        }
        case WM_MENUCHAR: {
            return MNC_CLOSE << 16;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            break;
        }
        case WM_SIZE: {
            global_windowDidResize = true;
            break;
        }

        case WM_SYSKEYDOWN: {
            if ((wparam == VK_RETURN) && (lparam & (1 << 29))) { // Alt key is pressed
                if (GetWindowLong(hwnd, GWL_STYLE) & WS_OVERLAPPEDWINDOW) {
                    MONITORINFO mi = {sizeof(mi)};
                    if (GetWindowRect(hwnd, &g_wndRect) &&
                        GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
                        SetWindowLong(hwnd, GWL_STYLE, g_wndStyle & ~WS_OVERLAPPEDWINDOW);
                        SetWindowPos(hwnd, HWND_TOP,
                                     mi.rcMonitor.left, mi.rcMonitor.top,
                                     mi.rcMonitor.right - mi.rcMonitor.left,
                                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                        ShowWindow(hwnd, SW_MAXIMIZE);
                    }
                } else {
                    SetWindowLong(hwnd, GWL_STYLE, g_wndStyle);
                    SetWindowPos(
                            hwnd, HWND_NOTOPMOST, g_wndRect.left, g_wndRect.top, g_wndRect.right - g_wndRect.left,
                            g_wndRect.bottom - g_wndRect.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
                    ShowWindow(hwnd, SW_NORMAL);
                }
            }
            break;
        }

        default:
            result = DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return result;
}

struct DrawCommand {
    float x1;
    float y1;
    float x2;
    float y2;
    float color1[3];
    float color2[3];
    float color3[3];
    float color4[3];
    float quant;
};


std::vector<DrawCommand> *the_input;
bool changedMode;
DXGI_FORMAT format;
bool hdr;
unsigned int flicker;

bool setMetadata;
DXGI_HDR_METADATA_HDR10 *metadata;

std::mutex m;
std::condition_variable cv;
std::atomic<bool> pending;

std::atomic<bool> debug;

void set_coords_from_window(DrawCommand &command, float windowSize) {
    float num = std::sqrt(windowSize / 100);

    command.x1 = -1 * num;
    command.y1 = 1 * num;
    command.x2 = 1 * num;
    command.y2 = -1 * num;
}

void set_colors_from_rgb(DrawCommand &command, int color[3], float maxV) {
    for (int i = 0; i < 3; i++) {
        command.color1[i] = color[i] / maxV;
        command.color2[i] = color[i] / maxV;
        command.color3[i] = color[i] / maxV;
        command.color4[i] = color[i] / maxV;
    }
    command.quant = 0;
}

void set_colors_from_rgb(DrawCommand &command, float color[3]) {
    for (int i = 0; i < 3; i++) {
        command.color1[i] = color[i];
        command.color2[i] = color[i];
        command.color3[i] = color[i];
        command.color4[i] = color[i];
    }
    command.quant = 0;
}

void populate_window_draw(DrawCommand &command, float windowSize, int color[3], float maxV) {
    set_coords_from_window(command, windowSize);
    set_colors_from_rgb(command, color, maxV);
}

void populate_window_draw(DrawCommand &command, float windowSize, float color[3]) {
    set_coords_from_window(command, windowSize);
    set_colors_from_rgb(command, color);
}

bool parse_window_command(std::stringstream &ss, DrawCommand &command, float maxV) {
    float windowSize;

    if (!(ss >> windowSize)) {
        return false;
    }

    if (windowSize <= 0 || windowSize > 100) {
        return false;
    }

    int color[3];
    if (!(ss >> color[0] >> color[1] >> color[2])) {
        return false;
    }

    for (int x: color) {
        if (x < 0 || x > maxV) {
            return false;
        }
    }

    populate_window_draw(command, windowSize, color, maxV);

    return true;
}

bool parse_draw_command(const std::string &command_str, DrawCommand &command, float maxV) {
    std::stringstream ss(command_str);

    std::string type;

    if (!(ss >> type)) {
        return false;
    }

    if (type == "window") {
        return parse_window_command(ss, command, maxV);
    }

    if (type != "draw") {
        return false;
    }

    if (!(ss >> command.x1 >> command.y1 >> command.x2 >> command.y2)) {
        return false;
    }

    std::vector<int> values;
    values.reserve(13);

    {
        int tmp;
        while (values.size() < 13 && ss >> tmp) {
            values.push_back(tmp);
        }
    }

    if (values.size() != 3 && values.size() != 13) {
        return false;
    }

    if (values.size() == 3) {
        set_colors_from_rgb(command, &values[0], maxV);
        command.quant = 0;
        return true;
    }

    for (int i = 0; i < 3; i++) {
        command.color1[i] = values[0 * 3 + i] / maxV;
        command.color2[i] = values[1 * 3 + i] / maxV;
        command.color3[i] = values[2 * 3 + i] / maxV;
        command.color4[i] = values[3 * 3 + i] / maxV;
    }

    command.quant = values[12] / maxV;

    return true;
}

bool parse_draw_string(const std::string &draw_string, std::vector<DrawCommand> &commands) {
    std::stringstream ss(draw_string);
    std::string command_str;

    int bits = 8;
    if (format == DXGI_FORMAT_R10G10B10A2_UNORM) {
        bits = 10;
    }
    auto maxV = (float) ((1 << bits) - 1);

    while (!draw_string.empty() && std::getline(ss, command_str, ';')) {
        DrawCommand command;
        if (parse_draw_command(command_str, command, maxV)) {
            commands.push_back(command);
        } else {
            // If any command fails to parse, return an empty vector
            commands.clear();
            return false;
        }
    }
    return true;
}

bool parse_mode_string(const std::string &mode_string, DXGI_FORMAT *format, bool *hdr) {
    std::stringstream ss(mode_string);
    std::string command_type;
    std::string mode;

    if (!(ss >> command_type >> mode)) {
        return false;
    }
    if (command_type != "mode") { // this should never happen
        return false;
    }

    if (mode == "8") {
        *format = DXGI_FORMAT_B8G8R8A8_UNORM;
        *hdr = false;
    } else if (mode == "8_hdr") {
        *format = DXGI_FORMAT_B8G8R8A8_UNORM;
        *hdr = true;
    } else if (mode == "10") {
        *format = DXGI_FORMAT_R10G10B10A2_UNORM;
        *hdr = false;
    } else if (mode == "10_hdr") {
        *format = DXGI_FORMAT_R10G10B10A2_UNORM;
        *hdr = true;
    } else {
        return false;
    }
    return true;
}

// terrible XML "parsing" code generated by ChatGPT

// Helper function to extract a numeric value from an attribute
template<typename T>
T getAttr(const std::string &xmlData, const std::string &attributeName, size_t startPos, size_t endPos) {
    size_t attributePos = xmlData.find(attributeName + "=\"", startPos);
    if (attributePos != std::string::npos && attributePos < endPos) {
        size_t valueStart = attributePos + attributeName.length() + 2;
        size_t valueEnd = xmlData.find("\"", valueStart);
        if (valueEnd != std::string::npos) {
            return static_cast<T>(std::stof(xmlData.substr(valueStart, valueEnd - valueStart)));
        }
    }
    return static_cast<T>(0); // Default value if attribute not found or parsing fails
}

void parseColorXML(const std::string &xmlData, size_t start, float &colorRed, float &colorGreen, float &colorBlue,
                   int &bits) {
    size_t endPos = xmlData.find('>', start);
    // if this is not colex: use subsequent colex, if it exists
    const char *colex = "<colex";
    if (xmlData.substr(start, std::strlen(colex)) != colex &&
        xmlData.substr(endPos + 1, std::strlen(colex)) == colex) {
        start = endPos + 1;
        endPos = xmlData.find('>', start);
    }
    bits = getAttr<int>(xmlData, "bits", start, endPos);
    if (bits == 0) {
        bits = 8;
    }
    int maxV = (1 << bits) - 1;
    colorRed = getAttr<float>(xmlData, "red", start, endPos) / maxV;
    colorGreen = getAttr<float>(xmlData, "green", start, endPos) / maxV;
    colorBlue = getAttr<float>(xmlData, "blue", start, endPos) / maxV;
}

void parseGeometryXML(const std::string &xmlData, size_t start, float &geometryX, float &geometryY, float &geometryCX,
                      float &geometryCY) {
    size_t endPos = xmlData.find('>', start);
    geometryX = getAttr<float>(xmlData, "x", start, endPos);
    geometryY = getAttr<float>(xmlData, "y", start, endPos);
    geometryCX = getAttr<float>(xmlData, "cx", start, endPos);
    geometryCY = getAttr<float>(xmlData, "cy", start, endPos);
}

void parseLightspaceCalibrationXML(const std::string &xmlData, float &colorRed, float &colorGreen, float &colorBlue,
                                   float &backgroundRed, float &backgroundGreen, float &backgroundBlue,
                                   float &geometryX, float &geometryY, float &geometryCX, float &geometryCY,
                                   int &targetBits) {
    // just making guesses based on how PGenerator parses this...
    bool hasBg = false;

    size_t firstRect = xmlData.find("<rectangle");
    size_t secondRect = xmlData.find("<rectangle", firstRect + 10);

    size_t firstColor = xmlData.find("<col", firstRect);
    size_t secondColor;

    if (secondRect != std::string::npos) {
        hasBg = true;
        secondColor = xmlData.find("<col", secondRect);
    }

    if (!hasBg) {
        parseColorXML(xmlData, firstColor, colorRed, colorGreen, colorBlue, targetBits);
        backgroundRed = backgroundGreen = backgroundBlue = 0;
        size_t geometryPos = xmlData.find("<geometry", firstRect);
        parseGeometryXML(xmlData, geometryPos, geometryX, geometryY, geometryCX, geometryCY);
    } else {
        int bgBits;
        parseColorXML(xmlData, firstColor, backgroundRed, backgroundGreen, backgroundBlue, bgBits);
        parseColorXML(xmlData, secondColor, colorRed, colorGreen, colorBlue, targetBits);
        size_t geometryPos = xmlData.find("<geometry", secondRect);
        parseGeometryXML(xmlData, geometryPos, geometryX, geometryY, geometryCX, geometryCY);
    }
}

void parseCalibrationXML(const std::string &xmlData, float &colorRed, float &colorGreen, float &colorBlue,
                         float &backgroundRed, float &backgroundGreen, float &backgroundBlue,
                         float &geometryX, float &geometryY, float &geometryCX, float &geometryCY, int &targetBits) {
    // Find the position of "color" tag
    size_t colorPos = xmlData.find("<color");
    if (colorPos != std::string::npos) {
        parseColorXML(xmlData, colorPos, colorRed, colorGreen, colorBlue, targetBits);
    }

    // Find the position of "background" tag
    size_t backgroundPos = xmlData.find("<background");
    if (backgroundPos != std::string::npos) {
        int bgBits;
        parseColorXML(xmlData, backgroundPos, backgroundRed, backgroundGreen, backgroundBlue, bgBits);
    }

    // Find the position of "geometry" tag
    size_t geometryPos = xmlData.find("<geometry");
    if (geometryPos != std::string::npos) {
        parseGeometryXML(xmlData, geometryPos, geometryX, geometryY, geometryCX, geometryCY);
    }
}

void drawPluge(bool hdr, bool tenbit, std::vector<DrawCommand> &commands) {
    const float maxV = tenbit ? 1023 : 255;

    auto idx = [](char x) { return x - 'a'; };

    // 4K UHDTV values from BT.814-4
    uint16_t higher = hdr ? 399 : 940;
    uint16_t black = 64;
    uint16_t lighter = 80;
    uint16_t darker = 48;

    uint16_t horz[] = {0, 624, 1199, 1776, 2063, 2640, 3215, 3839};
    uint16_t vert[] = {0, 648, 690, 935, 936, 1223, 1224, 1469, 1511, 2159};

    auto drawCoords = [&](uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t code) {
        const float width = 3840;
        const float height = 2160;

        auto cmd = DrawCommand{};

        float level = (code / (tenbit ? 1 : 4)) / maxV;

        for (int i = 0; i < 3; i++) {
            cmd.color1[i] = level;
            cmd.color2[i] = level;
            cmd.color3[i] = level;
            cmd.color4[i] = level;
        }

        cmd.x1 = -1 + 2 * x1 / width;
        cmd.y1 = 1 - 2 * y1 / height;
        cmd.x2 = -1 + 2 * (x2 + 1) / width;
        cmd.y2 = 1 - 2 * (y2 + 1) / height;

        commands.push_back(cmd);
    };

    auto draw = [&](char horz1, char vert1, char horz2, char vert2, uint16_t code) {
        auto x1 = horz[idx(horz1)];
        auto y1 = vert[idx(vert1)];
        auto x2 = horz[idx(horz2)];
        auto y2 = vert[idx(vert2)];

        drawCoords(x1, y1, x2, y2, code);
    };

    // black background
    draw('a', 'a', 'h', 'j', black);

    // higher square in the middle
    draw('d', 'e', 'e', 'f', higher);

    // lighter rectangle on the right
    draw('f', 'b', 'g', 'd', lighter);

    // darker rectangle on the right
    draw('f', 'g', 'g', 'i', darker);

    // 20 lighter/darker bars on the left
    for (int i = 0; i < 20; i++) {
        uint16_t x1 = horz[idx('b')];
        uint16_t y1 = vert[idx('c')] + 2 * 20 * i;
        uint16_t x2 = horz[idx('c')];
        uint16_t y2 = y1 + 19;

        uint16_t color = i < 10 ? lighter : darker;

        drawCoords(x1, y1, x2, y2, color);
    }
}

void drawBars(bool limited, std::vector<DrawCommand> &commands) {
    const float maxV = 1023;

    auto idx = [](char x) { return x - 'a'; };

    uint16_t bars[] = {1920, 1080, 240, 206, 204, 136, 70, 68, 238, 438, 282};
    auto bar = [&](char x) { return isupper(x) ? (uint16_t) (bars[idx(tolower(x))] / 2) : bars[idx(x)]; };

    auto drawCoords = [&](uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t rgb[3]) {
        const float width = bar('a');
        const float height = bar('b');

        auto cmd = DrawCommand{};

        for (int i = 0; i < 3; i++) {
            float level = rgb[i] / maxV;
            cmd.color1[i] = level;
            cmd.color2[i] = level;
            cmd.color3[i] = level;
            cmd.color4[i] = level;
        }

        cmd.x1 = -1 + 2 * x1 / width;
        cmd.y1 = 1 - 2 * y1 / height;
        cmd.x2 = -1 + 2 * x2 / width;
        cmd.y2 = 1 - 2 * y2 / height;

        commands.push_back(cmd);
    };

    int y = 0;

    {
        auto draw = [&](uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb[3]) {
            return drawCoords(x, y, x + w, y + h, rgb);
        };
        uint16_t colors[2][15][3] = {
                {{940,  940,  940},  {940,  940,  64}, {64, 940,  940},  {64, 940,  64}, {940,  64, 940},  {940,  64, 64}, {64, 64, 940},  {572, 572, 572}, {572, 572, 64}, {64, 572, 572}, {64, 572, 64}, {572, 64, 572}, {572, 64, 64}, {64, 64, 572}, {414, 414, 414}},
                {{1023, 1023, 1023}, {1023, 1023, 0},  {0,  1023, 1023}, {0,  1023, 0},  {1023, 0,  1023}, {1023, 0,  0},  {0,  0,  1023}, {593, 593, 593}, {593, 593, 0},  {0,  593, 593}, {0,  593, 0},  {593, 0,  593}, {593, 0,  0},  {0,  0,  593}, {409, 409, 409}}
        };
        auto color = [&](uint16_t idx) { return colors[limited ? 0 : 1][idx]; };
        char widths[7] = {'d', 'd', 'd', 'e', 'd', 'd', 'd'};
        uint16_t x = bar('c');


        uint16_t h1 = bar('b') / 12;
        uint16_t h2 = bar('b') / 2;
        int numBars = ARRAY_NUM(widths);
        int i;
        for (i = 0; i < numBars; i++) {
            uint16_t width = bar(widths[i]);
            draw(x, 0, width, h1, color(i));
            draw(x, h1, width, h2, color(i + numBars));
            x += width;
        }

        draw(0, 0, bar('c'), h1 + h2, color(i + numBars));
        draw(x, 0, bar('c'), h1 + h2, color(i + numBars));

        y += h1 + h2;
    }

    {
        uint16_t x = 0;
        uint16_t h = bar('b') / 12;

        auto draw = [&](uint16_t w, uint16_t level) {
            uint16_t rgb[3] = {level, level, level};
            drawCoords(x, y, x + w, y + h, rgb);
            x += w;
        };
        uint16_t levels[2][15] = {
                {572, 4, 64, 152, 239, 327, 414, 502, 590, 677, 765, 852, 940,  1019, 572},
                {593, 0, 0,  102, 205, 307, 409, 512, 614, 716, 818, 921, 1023, 1023, 593}
        };
        auto level = [&](uint16_t idx) { return levels[limited ? 0 : 1][idx]; };
        char widths[15] = {'c', 'd', 'D', 'D', 'D', 'D', 'E', 'E', 'D', 'D', 'D', 'D', 'D', 'D', 'c'};

        int numBars = ARRAY_NUM(widths);
        int i;
        for (i = 0; i < numBars; i++) {
            uint16_t width = bar(widths[i]);
            draw(width, level(i));
        }

        y += h;
    }

    {
        uint16_t x = 0;
        uint16_t h = bar('b') / 12;

        auto draw = [&](uint16_t w, uint16_t level) {
            uint16_t rgb[3] = {level, level, level};
            drawCoords(x, y, x + w, y + h, rgb);
            x += w;
        };
        uint16_t levels[2][15] = {
                {64, 4, 1019},
                {0,  0, 1023}
        };

        auto level = [&](uint16_t idx) { return levels[limited ? 0 : 1][idx]; };
        uint16_t widths[3] = {bar('c'), (uint16_t) (limited ? 559 : 551), 107};
        uint16_t gradientWidth = limited ? 1014 : 1022;

        int numBars = ARRAY_NUM(widths);
        for (int i = 0; i < numBars; i++) {
            uint16_t width = widths[i];
            draw(width, level(i));
            if (i == 1) {
                draw(gradientWidth, level(2));
                auto cmd = &commands.back();
                for (int j = 0; j < 3; j++) {
                    cmd->color1[j] = cmd->color3[j] = (level(1) + 1) / maxV;
                }
                cmd->quant = 1 / maxV;
            }
        }

        y += h;
    }

    {
        uint16_t x = 0;
        uint16_t h = bar('b') / 4;

        auto draw = [&](uint16_t w, uint16_t rgb[3]) {
            drawCoords(x, y, x + w, y + h, rgb);
            x += w;
        };

        auto drawGray = [&](uint16_t w, uint16_t level) {
            uint16_t rgb[3] = {level, level, level};
            draw(w, rgb);
        };

        uint16_t colors[2][6][3] = {
                {{568, 571, 381}, {484, 566, 571}, {474, 564, 368}, {536, 361, 564}, {530, 350, 256}, {317, 236, 562}},
                {{589, 592, 370}, {491, 586, 592}, {478, 584, 355}, {551, 347, 584}, {544, 334, 225}, {296, 201, 582}}
        };
        uint16_t grays[2][9] = {
                {64, 48, 64, 80, 64, 99, 64, 572, 64},
                {0,  0,  0,  20, 0,  41, 0,  593, 0}
        };

        auto color = [&](uint16_t idx) { return colors[limited ? 0 : 1][idx]; };
        auto gray = [&](uint16_t idx) { return grays[limited ? 0 : 1][idx]; };
        char widths[9] = {'f', 'g', 'h', 'g', 'h', 'g', 'i', 'j', 'k'};

        const int numColors = ARRAY_NUM(colors[0]);
        for (int i = 0; i < numColors / 2; i++) {
            draw(bar('c') / 3, color(i));
        }

        const int numGrays = ARRAY_NUM(grays[0]);
        for (int i = 0; i < numGrays; i++) {
            drawGray(bar(widths[i]), gray(i));
        }

        for (int i = numColors / 2; i < numColors; i++) {
            draw(bar('c') / 3, color(i));
        }
    }
}

void set_pending() {
    pending.store(true, std::memory_order_release);
}

void wait_pending() {
    std::unique_lock lk(m);
    cv.wait(lk, [] { return !pending.load(std::memory_order_acquire); });
}

void StartResolve(float window, const std::string &ip, uint16_t port, bool isHdr) {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return;
    }

    // Create a socket
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        WSACleanup();
        return;
    }

    // Set up the server address information
    sockaddr_in serverAddress = {};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    // Convert IP address from string to binary form
    if (inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr) <= 0) {
        std::cerr << "Invalid IP address" << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return;
    }

    std::cerr << "Attempting to connect to " << ip << ":" << port << std::endl;

    // Connect to the server
    if (connect(clientSocket, reinterpret_cast<sockaddr *>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to server" << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return;
    }

    std::cerr << "Connection established!" << std::endl;

    the_input = new std::vector<DrawCommand>;
    set_pending();

    while (true) {
        wait_pending(); // wait for pending stuff

        // Receive the data length
        int32_t dataLen;
        int bytesReceived = recv(clientSocket, reinterpret_cast<char *>(&dataLen), sizeof(dataLen), MSG_WAITALL);
        if (bytesReceived != sizeof(dataLen)) {
            std::cerr << "Failed to receive data length" << std::endl;
            goto cleanup;
        }

        // Convert the data length from network byte order to host byte order
        dataLen = ntohl(dataLen);
        if (dataLen <= 0) {
            std::cerr << "Server indicated connection close" << std::endl;
            goto cleanup;
        }

        // Receive the XML data
        std::string xmlData(dataLen, '\0');
        bytesReceived = recv(clientSocket, &xmlData[0], dataLen, MSG_WAITALL);
        if (bytesReceived != dataLen) {
            std::cerr << "Failed to receive XML data" << std::endl;
            goto cleanup;
        }

        // Variables to store parsed values
        float colorRed, colorGreen, colorBlue;
        float backgroundRed, backgroundGreen, backgroundBlue;
        float geometryX, geometryY, geometryCX, geometryCY;

        int targetBits;

        if (xmlData.find("<rectangle") != std::string::npos) {
            parseLightspaceCalibrationXML(xmlData, colorRed, colorGreen, colorBlue,
                                          backgroundRed, backgroundGreen, backgroundBlue,
                                          geometryX, geometryY, geometryCX, geometryCY, targetBits);
        } else {
            parseCalibrationXML(xmlData, colorRed, colorGreen, colorBlue,
                                backgroundRed, backgroundGreen, backgroundBlue,
                                geometryX, geometryY, geometryCX, geometryCY, targetBits);
        }

        if (targetBits != 8 && targetBits != 10) {
            std::cerr << "Unsupported bit depth, expected 8 or 10" << std::endl;
            continue;
        }

        bool isFullField = geometryX == 0 && geometryY == 0 && geometryCX == 1 && geometryCY == 1;

        auto commands = new std::vector<DrawCommand>;
        if (!isFullField && !(backgroundRed == 0 && backgroundGreen == 0 && backgroundBlue == 0)) {
            float bgColor[3] = {backgroundRed, backgroundGreen, backgroundBlue};
            DrawCommand background = {};
            populate_window_draw(background, 100, bgColor);
            commands->push_back(background);
        }
        {
            float color[3] = {colorRed, colorGreen, colorBlue};
            DrawCommand draw = {};
            set_colors_from_rgb(draw, color);

            if (window == 0 || isFullField) {
                // use supplied coordinates
                draw.x1 = -1 + 2 * geometryX;
                draw.y1 = 1 - 2 * geometryY;
                draw.x2 = draw.x1 + 2 * geometryCX;
                draw.y2 = draw.y1 - 2 * geometryCY;
            } else {
                // window override
                set_coords_from_window(draw, window);
            }

            commands->push_back(draw);
        }

        if (debug.load(std::memory_order_acquire)) {
            std::cerr << xmlData << std::endl;
        }

        the_input = commands;

        bool bitMatches = targetBits == 10 && format == DXGI_FORMAT_R10G10B10A2_UNORM ||
                          targetBits == 8 && format == DXGI_FORMAT_B8G8R8A8_UNORM;

        static bool firstPattern = true;
        if (firstPattern || !bitMatches) {
            if (targetBits == 8) {
                format = DXGI_FORMAT_B8G8R8A8_UNORM;
            } else {
                format = DXGI_FORMAT_R10G10B10A2_UNORM;
            }
            hdr = isHdr;
            changedMode = true;

            std::cerr << "Switching to " << targetBits << " bit " << (isHdr ? "HDR" : "SDR") << " output" << std::endl;
            if (firstPattern && window != 0) {
                std::cerr << "Using " << window << "% window instead of provided coordinates" << std::endl;
            }
            firstPattern = false;
        }
        set_pending();
    }

    cleanup:
    // Close the socket and cleanup
    closesocket(clientSocket);
    WSACleanup();
    the_input = new std::vector<DrawCommand>;
    set_pending();
}

const int MAX_BUFFER_SIZE = 1024;

std::atomic<bool> discoveryActive;

void PGenDiscoveryHandler(SOCKET udpSocket) {
    sockaddr_in clientAddr = {};
    int clientAddrSize = sizeof(clientAddr);

    while (true) {
        char buffer[MAX_BUFFER_SIZE];
        int bytesRead = recvfrom(udpSocket, buffer, MAX_BUFFER_SIZE - 1, 0, reinterpret_cast<sockaddr *>(&clientAddr),
                                 &clientAddrSize);

        if (bytesRead > 0) {
            buffer[bytesRead] = '\0'; // Null-terminate the received data
            if (!strcmp(buffer, "Who is a PGenerator")) {
                const char *response = "This is dogegen :)";
                sendto(udpSocket, response, strlen(response), 0, reinterpret_cast<sockaddr *>(&clientAddr),
                       clientAddrSize);
                std::cerr << "Sent discovery response" << std::endl;
            }
        } else if (!discoveryActive) {
            return;
        } else {
            std::cerr << "Error while receiving UDP data" << std::endl;
        }
    }
}

void StartPGen(bool isHdr, int passive[3]) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return;
    }

    // Create UDP socket
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket == INVALID_SOCKET) {
        std::cerr << "Error creating UDP socket" << std::endl;
        WSACleanup();
        return;
    }

    sockaddr_in udpAddr;
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_addr.s_addr = INADDR_ANY;
    udpAddr.sin_port = htons(1977); // Listen on port 1977

    // Bind UDP socket
    if (bind(udpSocket, reinterpret_cast<sockaddr *>(&udpAddr), sizeof(udpAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed for UDP socket" << std::endl;
        closesocket(udpSocket);
        WSACleanup();
        return;
    }

    // Create a thread to handle UDP connections
    discoveryActive = true;
    auto *udpThread = new std::thread(PGenDiscoveryHandler, udpSocket);

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(85); // Listen on port 85

    SOCKET serverSocket;
    bool switchedMode = false;

    const auto maxV = (float) ((1 << 8) - 1);

    std::vector<DrawCommand> passiveV;
    if (passive) {
        DrawCommand draw;
        populate_window_draw(draw, 100, passive, maxV);
        passiveV.push_back(draw);
    }

    while (true) {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "Error creating server socket" << std::endl;
            goto cleanup;
        }

        // Enable address reuse
        int enableReuse = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&enableReuse), sizeof(int)) ==
            SOCKET_ERROR) {
            std::cerr << "Setsockopt failed" << std::endl;
            goto cleanup;
        }

        if (bind(serverSocket, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed" << std::endl;
            goto cleanup;
        }

        if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "Listen failed" << std::endl;
            goto cleanup;
        }

        wait_pending(); // wait for pending stuff

        if (!switchedMode) {
            format = DXGI_FORMAT_B8G8R8A8_UNORM;
            hdr = isHdr;
            changedMode = true;

            std::cerr << "Switching to 8 bit " << (isHdr ? "HDR" : "SDR") << " output" << std::endl;
            switchedMode = true;
        }

        the_input = new std::vector<DrawCommand>(passiveV); // draw passive patch while waiting for connection
        set_pending();

        std::cerr << "Waiting for incoming connection..." << std::endl;

        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed" << std::endl;
            goto cleanup;
        }

        std::cerr << "Client connected. Closing server socket." << std::endl;
        closesocket(serverSocket);

        while (true) {
            wait_pending(); // wait for pending stuff

            // Read messages from the client
            char buffer[MAX_BUFFER_SIZE];
            int bytesRead = 0;

            bool error = false;
            bool closed = false;

            while (true) {
                int result = recv(clientSocket, buffer + bytesRead, 1, 0);
                if (result > 0 && bytesRead != MAX_BUFFER_SIZE - 1) {
                    if (buffer[bytesRead] == 0x0d && buffer[bytesRead - 1] == 0x2) {
                        buffer[bytesRead - 1] = '\0';
                        break;
                    }
                    bytesRead++;
                } else if (result == 0) {
                    std::cerr << "Client disconnected." << std::endl;
                    closed = true;
                    break;
                } else {
                    std::cerr << "Error while receiving data" << std::endl;
                    error = true;
                    break;
                }
            }

            if (closed || error) {
                closesocket(clientSocket);
                break;
            }

            std::string command(buffer);

            if (debug.load(std::memory_order_acquire)) {
                std::cerr << command << std::endl;
            }

            const char *response = nullptr;

            if (command == "CMD:GET_RESOLUTION") {
                response = "OK:3840x2160";
            } else if (command == "CMD:GET_GPU_MEMORY") {
                response = "OK:192";
            } else if (command == "TESTTEMPLATE:PatternDynamic:0,0,0") {
                the_input = new std::vector<DrawCommand>(passiveV);  // done displaying patterns
            } else if (command.rfind("RGB=RECTANGLE", 0) == 0) {
                int screenWidth = 3840;
                int screenHeight = 2160;

                std::istringstream ss(command);

                int width, height, idk, r, g, b, bg_r, bg_g, bg_b;

                char d;
                ss.ignore(std::numeric_limits<std::streamsize>::max(), ';');  // Skip "RGB=RECTANGLE;"
                ss >> width >> d >> height >> d >> idk >> d >> r >> d >> g >> d >> b >> d >> bg_r >> d >> bg_g >> d
                   >> bg_b;

                // Check for any extraction errors
                if (ss.fail()) {
                    std::cerr << "Failed to parse RGB=RECTANGLE command" << std::endl;
                } else {
                    auto commands = new std::vector<DrawCommand>;
                    int bgColor[3] = {bg_r, bg_g, bg_b};
                    DrawCommand background = {};
                    populate_window_draw(background, 100, bgColor, maxV);
                    commands->push_back(background);

                    int color[3] = {r, g, b};
                    DrawCommand draw = {};
                    set_colors_from_rgb(draw, color, maxV);

                    // calculate coordinates based on supplied width and height
                    draw.x1 = -1.0f * width / screenWidth;
                    draw.y1 = 1.0f * height / screenHeight;
                    draw.x2 = -1 * draw.x1;
                    draw.y2 = -1 * draw.y1;

                    commands->push_back(draw);
                    the_input = commands;
                }
            } else if (command.rfind("RGB=TEXT", 0) == 0 || command.rfind("RGB=IMAGE", 0) == 0) {
                // ignore
            } else {
                the_input = new std::vector<DrawCommand>; // draw nothing
            }

            set_pending();

            if (response) {
                send(clientSocket, response, (int) strlen(response) + 1, 0);
            }

        }
        std::cerr << "Client disconnected. Reopening server socket." << std::endl;
        closesocket(clientSocket);
    }
    cleanup:
    discoveryActive = false;
    if (udpSocket != INVALID_SOCKET) {
        closesocket(udpSocket);
    }
    if (udpThread) {
        udpThread->join();
    }
    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
    }
    if (switchedMode) {
        wait_pending(); // wait for pending stuff
        the_input = new std::vector<DrawCommand>();
        set_pending();
    }
    WSACleanup();
}

void InputReader(char *cmds[], int num_cmds) {
    bool print_ok = false;
    int cmds_processed = 0;
    while (true) {
        wait_pending(); // wait for pending stuff
        if (print_ok) {
            std::cout << "ok" << std::endl;
            print_ok = false;
        }
        std::cout << "> " << std::flush;
        std::string input;
        if (cmds_processed < num_cmds) {
            input = std::string(cmds[cmds_processed++]);
            std::cout << input << std::endl;
        } else {
            getline(std::cin, input);
        }
        std::stringstream ss(input);
        std::string command_type;
        ss >> command_type;
        if (command_type == "mode") {
            if (parse_mode_string(input, &format, &hdr)) {
                the_input = new std::vector<DrawCommand>;
                print_ok = true;
                changedMode = true;
                set_pending();
            } else {
                std::cout << "error: invalid mode" << std::endl;
            }
        } else if (command_type.rfind("resolve", 0) == 0) { // starts with resolve
            bool isHdr;

            if (command_type == "resolve_hdr") {
                isHdr = true;
            } else if (command_type == "resolve_sdr") {
                isHdr = false;
            } else {
                std::cout << "error: must specify resolve_sdr or resolve_hdr" << std::endl;
                continue;
            }

            std::string ip = "127.0.0.1";
            uint16_t port = 20002;

            std::string arg1;
            bool arg1IsWindow = false;
            if (ss >> arg1) {
                if (arg1.rfind("localhost", 0) == 0 || std::count(arg1.begin(), arg1.end(), '.') > 1) {
                    // looks like an IP address
                    size_t colonPos = arg1.find(':');

                    if (colonPos == std::string::npos) {
                        ip = arg1;
                    } else {
                        ip = arg1.substr(0, colonPos);

                        std::istringstream ss(arg1.substr(colonPos + 1));
                        if (!(ss >> port)) {
                            std::cout << "error: invalid port" << std::endl;
                            continue;
                        }
                    }
                } else {
                    arg1IsWindow = true;
                }
            }

            if (ip == "localhost") {
                ip = "127.0.0.1";
            }

            float window = 0;

            float tmp;
            if (arg1IsWindow || ss >> tmp) {
                if (arg1IsWindow) {
                    std::istringstream ss(arg1);
                    if (!(ss >> tmp)) {
                        std::cout << "error: argument must be IP or window size" << std::endl;
                        continue;
                    }
                }
                if (tmp > 0 && tmp <= 100) {
                    window = tmp;
                } else {
                    std::cout << "error: window size must be >0 and <=100" << std::endl;
                    continue;
                }
            }

            StartResolve(window, ip, port, isHdr);
        } else if (command_type.rfind("pgen", 0) == 0) { // starts with pgen
            bool isHdr;

            if (command_type == "pgen_hdr") {
                isHdr = true;
            } else if (command_type == "pgen_sdr") {
                isHdr = false;
            } else {
                std::cout << "error: must specify pgen_sdr or pgen_hdr" << std::endl;
                continue;
            }

            int *p = nullptr;
            if (!ss.eof()) {
                p = new int[3];
                if (!(ss >> p[0] >> p[1] >> p[2])) {
                    std::cout << "error: must specify r g b" << std::endl;
                    continue;
                } else {
                    bool invalid = false;
                    for (int i = 0; i < 3; i++) {
                        if (p[i] < 0 || p[i] > 255) {
                            std::cout << "error: invalid rgb values" << std::endl;
                            invalid = true;
                            break;
                        }
                    }
                    if (invalid) {
                        continue;
                    }
                }
            }

            StartPGen(isHdr, p);
            delete[] p;
        } else if (command_type == "flicker") {
            unsigned int tmp;

            if (!(ss >> tmp)) {
                std::cout << "error: must specify number of black frames";
            } else {
                flicker = tmp;
                print_ok = true;
                set_pending();
            }
        } else if (command_type == "debug") {
            unsigned int tmp;
            if (!(ss >> tmp) || tmp != 0 && tmp != 1) {
                std::cout << "error: must specify 0 or 1" << std::endl;
            } else {
                debug.store(tmp, std::memory_order_release);
                print_ok = true;
            }
        } else if (command_type == "maxcll") {
            int maxCLL;
            if (!(ss >> maxCLL) || maxCLL < -1 || maxCLL > 10000) {
                std::cout << "error: must specify value between -1 and 10000" << std::endl;
                continue;
            }
            if (maxCLL != -1) {
                unsigned maxFALL, maxDML;
                if (!(ss >> maxFALL) && ss.eof() && maxFALL != UINT_MAX) {
                    maxFALL = maxDML = maxCLL;
                } else {
                    if (maxFALL > 10000) {
                        std::cout << "error: must specify value between 0 and 10000" << std::endl;
                        continue;
                    }
                    if (!(ss >> maxDML) || maxDML > 10000) {
                        std::cout << "error: must specify value between 0 and 10000" << std::endl;
                        continue;
                    }
                }
                metadata = new DXGI_HDR_METADATA_HDR10{};
                metadata->MaxMasteringLuminance = maxDML;
                metadata->MaxContentLightLevel = maxCLL;
                metadata->MaxFrameAverageLightLevel = maxFALL;
            }
            setMetadata = true;
            print_ok = true;
            set_pending();
        } else if (command_type.rfind("pluge", 0) == 0) {
            bool useHdr;
            if (command_type == "pluge") {
                useHdr = hdr;
            } else if (command_type == "pluge_hdr") {
                useHdr = true;
            } else {
                std::cout << "error: must specify pluge or pluge_hdr" << std::endl;
                continue;
            }

            const int bits = format == DXGI_FORMAT_R10G10B10A2_UNORM ? 10 : 8;

            if (bits == 8 && useHdr) {
                std::cout << "error: hdr pluge requires a 10 bit mode" << std::endl;
                continue;
            }
            auto tmp = new std::vector<DrawCommand>;
            drawPluge(useHdr, bits == 10, *tmp);
            the_input = tmp;
            print_ok = true;
            set_pending();
        } else if (command_type.rfind("bars", 0) == 0) {
            bool limited;
            if (command_type == "bars_full") {
                limited = false;
            } else if (command_type == "bars_limited") {
                limited = true;
            } else {
                std::cout << "error: must specify bars_full or bars_limited" << std::endl;
                continue;
            }

            if (format != DXGI_FORMAT_R10G10B10A2_UNORM) {
                std::cout << "error: bars requires a 10 bit mode" << std::endl;
                continue;
            }
            auto tmp = new std::vector<DrawCommand>;
            drawBars(limited, *tmp);
            the_input = tmp;
            print_ok = true;
            set_pending();
        } else if (command_type == "draw" || command_type == "window" || command_type.empty()) {
            auto tmp = new std::vector<DrawCommand>;
            if (parse_draw_string(input, *tmp)) {
                print_ok = true;
                the_input = tmp;
                set_pending();
            } else {
                delete tmp;
                std::cout << "error: invalid draw command(s)" << std::endl;
            }
        } else {
            std::cout << "error: unrecognized command" << std::endl;
        }
    }
}

void updateColorSpace(IDXGISwapChain3 *swapChain) {
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    if (hdr) {
        colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    }
    HRESULT hResult = swapChain->SetColorSpace1(colorSpace);
    assert(SUCCEEDED(hResult));
}

int main(int argc, char *argv[]) {
    // default values
    hdr = false;
    format = DXGI_FORMAT_B8G8R8A8_UNORM;

    std::locale::global(std::locale("C"));

    // disable OS scaling
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // prevent display from turning off
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);

    std::thread t1(InputReader, &argv[1], argc - 1);

    HINSTANCE hInstance = GetModuleHandle(0);

    // Open a window
    HWND hwnd;
    {
        WNDCLASSEXW winClass = {};
        winClass.cbSize = sizeof(WNDCLASSEXW);
        winClass.style = CS_HREDRAW | CS_VREDRAW;
        winClass.lpfnWndProc = &WndProc;
        winClass.hInstance = hInstance;
        winClass.hIcon = LoadIconW(0, IDI_APPLICATION);
        // winClass.hCursor = LoadCursorW(0, IDC_ARROW);
        winClass.hCursor = NULL;
        winClass.lpszClassName = L"MyWindowClass";
        winClass.hIconSm = LoadIconW(0, IDI_APPLICATION);

        if (!RegisterClassExW(&winClass)) {
            MessageBoxA(0, "RegisterClassEx failed", "Fatal Error", MB_OK);
            return GetLastError();
        }

        int clientWidth = 1920;
        int clientHeight = 1080;

        RECT initialRect = {0, 0, clientWidth, clientHeight};

        AdjustWindowRect(&initialRect, g_wndStyle, FALSE);
        LONG initialWidth = initialRect.right - initialRect.left;
        LONG initialHeight = initialRect.bottom - initialRect.top;

        hwnd = CreateWindowW(winClass.lpszClassName,
                             L"dogegen",
                             g_wndStyle,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             initialWidth,
                             initialHeight,
                             0, 0, hInstance, 0);

        if (!hwnd) {
            MessageBoxA(0, "CreateWindowEx failed", "Fatal Error", MB_OK);
            return GetLastError();
        }

        HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFO);
        if (GetMonitorInfo(hMonitor, &monitorInfo)) {
            RECT work = monitorInfo.rcWork;

            int monitorWidth = work.right - work.left;
            int monitorHeight = work.bottom - work.top;

            // scale client area to get equivalent area as on 4k display, retaining aspect ratio
            double scale = sqrt((double) monitorWidth * monitorHeight / (3840 * 2160));
            clientWidth = (int) round(clientWidth * scale);
            clientHeight = (int) round(clientHeight * scale);

            // Calculate the centered position
            int centerX = (work.left + work.right - clientWidth) / 2;
            int centerY = (work.top + work.bottom - clientHeight) / 2;

            RECT center = {centerX, centerY, centerX + clientWidth, centerY + clientHeight};

            AdjustWindowRect(&center, g_wndStyle, FALSE);

            // Update window position
            SetWindowPos(hwnd, 0, center.left, center.top, center.right - center.left, center.bottom - center.top,
                         SWP_NOZORDER);
        }

        const int corner_preference = 1; // DWMWCP_DONOTROUND
        const DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_preference, sizeof(corner_preference));

        // Show the window
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
    }

    // Create D3D11 Device and Context
    ID3D11Device1 *d3d11Device;
    ID3D11DeviceContext1 *deviceContext;
    {
        ID3D11Device *baseDevice;
        ID3D11DeviceContext *baseDeviceContext;
        D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0};
        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hResult = D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE,
                                            0, creationFlags,
                                            featureLevels, ARRAYSIZE(featureLevels),
                                            D3D11_SDK_VERSION, &baseDevice,
                                            0, &baseDeviceContext);
        if (FAILED(hResult)) {
            MessageBoxA(0, "D3D11CreateDevice() failed", "Fatal Error", MB_OK);
            return GetLastError();
        }

        // Get 1.1 interface of D3D11 Device and Context
        hResult = baseDevice->QueryInterface(__uuidof(ID3D11Device1), (void **) &d3d11Device);
        assert(SUCCEEDED(hResult));
        baseDevice->Release();

        hResult = baseDeviceContext->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **) &deviceContext);
        assert(SUCCEEDED(hResult));
        baseDeviceContext->Release();
    }

#ifndef NDEBUG
    // Set up debug layer to break on D3D11 errors
    ID3D11Debug *d3dDebug = nullptr;
    d3d11Device->QueryInterface(__uuidof(ID3D11Debug), (void **) &d3dDebug);
    if (d3dDebug) {
        ID3D11InfoQueue *d3dInfoQueue = nullptr;
        if (SUCCEEDED(d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void **) &d3dInfoQueue))) {
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
            d3dInfoQueue->Release();
        }
        d3dDebug->Release();
    }
#endif

    // Create Swap Chain
    IDXGISwapChain4 *d3d11SwapChain;
    {
        // Get DXGI Factory (needed to create Swap Chain)
        IDXGIFactory2 *dxgiFactory;
        {
            IDXGIDevice1 *dxgiDevice;
            HRESULT hResult = d3d11Device->QueryInterface(__uuidof(IDXGIDevice1), (void **) &dxgiDevice);
            assert(SUCCEEDED(hResult));

            //dxgiDevice->SetMaximumFrameLatency(1);

            IDXGIAdapter *dxgiAdapter;
            hResult = dxgiDevice->GetAdapter(&dxgiAdapter);
            assert(SUCCEEDED(hResult));
            dxgiDevice->Release();

            DXGI_ADAPTER_DESC adapterDesc;
            dxgiAdapter->GetDesc(&adapterDesc);

            hResult = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void **) &dxgiFactory);
            assert(SUCCEEDED(hResult));
            dxgiAdapter->Release();
        }

        DXGI_SWAP_CHAIN_DESC1 d3d11SwapChainDesc = {};
        d3d11SwapChainDesc.Width = 0; // use window width
        d3d11SwapChainDesc.Height = 0; // use window height
        d3d11SwapChainDesc.Format = format;
        d3d11SwapChainDesc.SampleDesc.Count = 1;
        d3d11SwapChainDesc.SampleDesc.Quality = 0;
        d3d11SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d3d11SwapChainDesc.BufferCount = 2;
        d3d11SwapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        d3d11SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        d3d11SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        d3d11SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

        IDXGISwapChain1 *tmpSwapChain;

        HRESULT hResult = dxgiFactory->CreateSwapChainForHwnd(d3d11Device, hwnd, &d3d11SwapChainDesc, 0, 0,
                                                              &tmpSwapChain);
        assert(SUCCEEDED(hResult));

        // this needs to come after creating the swapchain
        dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES);

        hResult = tmpSwapChain->QueryInterface(__uuidof(IDXGISwapChain4), (LPVOID *) &d3d11SwapChain);
        assert(SUCCEEDED(hResult));
        tmpSwapChain->Release();

        updateColorSpace(d3d11SwapChain);

        dxgiFactory->Release();

    }

    // Create Framebuffer Render Target
    ID3D11RenderTargetView *d3d11FrameBufferView;
    {
        ID3D11Texture2D *d3d11FrameBuffer;
        HRESULT hResult = d3d11SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **) &d3d11FrameBuffer);
        assert(SUCCEEDED(hResult));

        hResult = d3d11Device->CreateRenderTargetView(d3d11FrameBuffer, 0, &d3d11FrameBufferView);
        assert(SUCCEEDED(hResult));
        d3d11FrameBuffer->Release();
    }

    // Create Vertex Shader
    ID3DBlob *vsBlob;
    ID3D11VertexShader *vertexShader;
    {
        ID3DBlob *shaderCompileErrorsBlob;
        HRESULT hResult = D3DCompile(shaders, sizeof(shaders) - 1, NULL, NULL, NULL, "vs_main", "vs_5_0", 0, 0, &vsBlob,
                                     &shaderCompileErrorsBlob);
        if (FAILED(hResult)) {
            if (shaderCompileErrorsBlob) {
                MessageBoxA(0, (const char *) shaderCompileErrorsBlob->GetBufferPointer(), "Shader Compiler Error",
                            MB_ICONERROR | MB_OK);
                shaderCompileErrorsBlob->Release();
            }
            return 1;
        }

        hResult = d3d11Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                                                  &vertexShader);
        assert(SUCCEEDED(hResult));
    }

    // Create Pixel Shader
    ID3D11PixelShader *pixelShader;
    {
        ID3DBlob *psBlob;
        ID3DBlob *shaderCompileErrorsBlob;
        HRESULT hResult = D3DCompile(shaders, sizeof(shaders) - 1, NULL, NULL, NULL, "ps_main", "ps_5_0", 0, 0, &psBlob,
                                     &shaderCompileErrorsBlob);
        if (FAILED(hResult)) {
            if (shaderCompileErrorsBlob) {
                MessageBoxA(0, (const char *) shaderCompileErrorsBlob->GetBufferPointer(), "Shader Compiler Error",
                            MB_ICONERROR | MB_OK);
                shaderCompileErrorsBlob->Release();
            }
            return 1;
        }

        hResult = d3d11Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                                                 &pixelShader);
        assert(SUCCEEDED(hResult));
        psBlob->Release();
    }

    // Create Input Layout
    ID3D11InputLayout *inputLayout;
    {
        D3D11_INPUT_ELEMENT_DESC inputElementDesc[] =
                {
                        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0},
                        {"COL",      0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
                        {"COL",      1, DXGI_FORMAT_R32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},

                };

        HRESULT hResult = d3d11Device->CreateInputLayout(inputElementDesc, ARRAYSIZE(inputElementDesc),
                                                         vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                                         &inputLayout);
        assert(SUCCEEDED(hResult));
        vsBlob->Release();
    }

    // Create Vertex Buffer
    ID3D11Buffer *vertexBuffer;
    UINT numVerts;
    UINT stride;
    UINT offset;
    {
        stride = 6 * sizeof(float);
        numVerts = 4;
        offset = 0;

        D3D11_BUFFER_DESC vertexBufferDesc = {};
        vertexBufferDesc.ByteWidth = 4 * stride;
        vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hResult = d3d11Device->CreateBuffer(&vertexBufferDesc, nullptr, &vertexBuffer);
        assert(SUCCEEDED(hResult));
    }

    auto frameLatencyWaitableObject = d3d11SwapChain->GetFrameLatencyWaitableObject();

    // Main Loop
    bool isRunning = true;
    while (isRunning) {
        DWORD result = WaitForSingleObjectEx(
                frameLatencyWaitableObject,
                1000, // 1 second timeout (shouldn't ever occur)
                true
        );

        MSG msg = {};
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                isRunning = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        bool doStuff = false;

        if (pending.load(std::memory_order_acquire)) {
            // loop to maybe work around spurious mutex failures
            for (int i = 0; i < 10; i++) {
                if (m.try_lock()) {
                    doStuff = true;
                    break;
                }
            }
        }

        if (global_windowDidResize || doStuff && changedMode) {
            deviceContext->OMSetRenderTargets(0, 0, 0);
            d3d11FrameBufferView->Release();

            DXGI_SWAP_CHAIN_DESC1 desc = {};
            d3d11SwapChain->GetDesc1(&desc);
            HRESULT res = d3d11SwapChain->ResizeBuffers(0, 0, 0, format, desc.Flags);
            assert(SUCCEEDED(res));

            ID3D11Texture2D *d3d11FrameBuffer;
            res = d3d11SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **) &d3d11FrameBuffer);
            assert(SUCCEEDED(res));

            res = d3d11Device->CreateRenderTargetView(d3d11FrameBuffer, NULL,
                                                      &d3d11FrameBufferView);
            assert(SUCCEEDED(res));
            d3d11FrameBuffer->Release();

            if (doStuff && changedMode) {
                updateColorSpace(d3d11SwapChain);
                flicker = 0;
                changedMode = false;
            }

            global_windowDidResize = false;
        }

        if (doStuff && setMetadata) {
            if (metadata) {
                d3d11SwapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(*metadata), metadata);
                delete metadata;
                metadata = nullptr;
            } else {
                d3d11SwapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
            }
            setMetadata = false;
        }

        // example: draw -1 1 1 -1 0 0 0 256 256 256 0 0 0 256 256 256 1
        static auto commands = new std::vector<DrawCommand>;
        if (doStuff && the_input != nullptr) {
            delete commands;
            commands = the_input;
            the_input = nullptr;
        }

        FLOAT backgroundColor[4] = {0, 0, 0, 1.0f};
        deviceContext->ClearRenderTargetView(d3d11FrameBufferView, backgroundColor);

        RECT winRect;
        GetClientRect(hwnd, &winRect);
        D3D11_VIEWPORT viewport = {0.0f, 0.0f, (FLOAT) (winRect.right - winRect.left),
                                   (FLOAT) (winRect.bottom - winRect.top), 0.0f, 1.0f};
        deviceContext->RSSetViewports(1, &viewport);

        deviceContext->OMSetRenderTargets(1, &d3d11FrameBufferView, nullptr);

        deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        deviceContext->IASetInputLayout(inputLayout);

        deviceContext->VSSetShader(vertexShader, nullptr, 0);
        deviceContext->PSSetShader(pixelShader, nullptr, 0);

        static unsigned int flickerCycle;
        static unsigned int flickerCounter;

        if (doStuff && flickerCycle != flicker) {
            flickerCycle = flicker;
            flickerCounter = 0;
        }

        for (auto &c: *commands) {
            if (flickerCycle != 0 && flickerCounter != flickerCycle) break;

            D3D11_MAPPED_SUBRESOURCE resource;
            deviceContext->Map(vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
            float the[4][6] = {
                    c.x1, c.y1, c.color1[0], c.color1[1], c.color1[2], c.quant,
                    c.x2, c.y1, c.color2[0], c.color2[1], c.color2[2], c.quant,
                    c.x1, c.y2, c.color3[0], c.color3[1], c.color3[2], c.quant,
                    c.x2, c.y2, c.color4[0], c.color4[1], c.color4[2], c.quant,
            };

            memcpy(resource.pData, the, stride * numVerts);
            deviceContext->Unmap(vertexBuffer, 0);

            deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

            deviceContext->Draw(numVerts, 0);
        }

        if (flickerCycle != 0) {
            flickerCounter += 1;
            flickerCounter %= flickerCycle + 1;
        }

        d3d11SwapChain->Present(1, 0);

        if (doStuff) {
            pending.store(false, std::memory_order_release);
            m.unlock();
            cv.notify_one();
        }
    }

    exit(0);
}
