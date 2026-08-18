#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000003
#endif
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Gdiplus;

namespace {

constexpr float kWidth = 452.0f;
constexpr float kHeight = 748.0f;
constexpr float kShellRadius = 38.0f;
constexpr int kMaxHistory = 50;

struct FRect {
    float x, y, w, h;
    bool Contains(float px, float py) const {
        return px >= x && py >= y && px <= x + w && py <= y + h;
    }
};

struct HistoryItem {
    std::wstring expression;
    std::wstring result;
};

struct ButtonDef {
    const wchar_t* label;
    const wchar_t* action;
    int row;
    int column;
    int span;
    bool isOperator;
    bool isClear;
};

constexpr ButtonDef kButtons[] = {
    {L"C",  L"C",    0, 0, 1, false, true},
    {L"CE", L"CE",   0, 1, 1, false, false},
    {L"←",  L"Back", 0, 2, 1, false, false},
    {L"÷",  L"/",    0, 3, 1, true,  false},
    {L"7",  L"7",    1, 0, 1, false, false},
    {L"8",  L"8",    1, 1, 1, false, false},
    {L"9",  L"9",    1, 2, 1, false, false},
    {L"×",  L"*",    1, 3, 1, true,  false},
    {L"4",  L"4",    2, 0, 1, false, false},
    {L"5",  L"5",    2, 1, 1, false, false},
    {L"6",  L"6",    2, 2, 1, false, false},
    {L"−",  L"-",    2, 3, 1, true,  false},
    {L"1",  L"1",    3, 0, 1, false, false},
    {L"2",  L"2",    3, 1, 1, false, false},
    {L"3",  L"3",    3, 2, 1, false, false},
    {L"+",  L"+",    3, 3, 1, true,  false},
    {L"0",  L"0",    4, 0, 2, false, false},
    {L"·",  L".",    4, 2, 1, false, false},
    {L"=",  L"=",    4, 3, 1, true,  false},
};

enum HitCode {
    HitNone = -1,
    HitClose = -2,
    HitMinimize = -3,
    HitDrag = -4,
    HitScrollThumb = -5,
    HitScrollTrack = -6,
    HitClearHistory = -7,
};

HWND g_window = nullptr;
ULONG_PTR g_gdiplusToken = 0;
float g_scale = 1.0f;
int g_hover = HitNone;
int g_pressed = HitNone;
bool g_trackingMouse = false;
bool g_draggingScroll = false;
float g_scrollGrab = 0.0f;
float g_historyOffset = 0.0f;
std::vector<HistoryItem> g_history;

std::wstring g_input = L"0";
std::wstring g_display = L"0";
std::wstring g_expression;
double g_accumulator = 0.0;
double g_lastOperand = 0.0;
bool g_hasAccumulator = false;
bool g_hasLastOperand = false;
bool g_startNewInput = true;
bool g_justEvaluated = false;
bool g_hasError = false;
std::wstring g_pendingOperator;
std::wstring g_lastOperator;

void AddRoundedRect(GraphicsPath& path, const FRect& r, float radius) {
    const float d = radius * 2.0f;
    path.StartFigure();
    path.AddArc(r.x, r.y, d, d, 180.0f, 90.0f);
    path.AddArc(r.x + r.w - d, r.y, d, d, 270.0f, 90.0f);
    path.AddArc(r.x + r.w - d, r.y + r.h - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.x, r.y + r.h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRounded(Graphics& graphics, const FRect& rect, float radius, Brush& brush) {
    GraphicsPath path;
    AddRoundedRect(path, rect, radius);
    graphics.FillPath(&brush, &path);
}

void StrokeRounded(Graphics& graphics, const FRect& rect, float radius, Pen& pen) {
    GraphicsPath path;
    AddRoundedRect(path, rect, radius);
    graphics.DrawPath(&pen, &path);
}

void FillGradientRounded(Graphics& graphics, const FRect& rect, float radius,
                         const Color* colors, const REAL* positions, int count) {
    LinearGradientBrush brush(PointF(rect.x, rect.y), PointF(rect.x + rect.w, rect.y + rect.h),
                              colors[0], colors[count - 1]);
    brush.SetInterpolationColors(colors, positions, count);
    FillRounded(graphics, rect, radius, brush);
}

void DrawTextLine(Graphics& graphics, const std::wstring& text, const FRect& rect,
                  float size, INT style, Color color,
                  StringAlignment horizontal = StringAlignmentNear,
                  StringAlignment vertical = StringAlignmentCenter) {
    Font font(L"Microsoft YaHei UI", size, style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format(StringFormat::GenericTypographic());
    format.SetAlignment(horizontal);
    format.SetLineAlignment(vertical);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    RectF layout(rect.x, rect.y, rect.w, rect.h);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, layout, &format, &brush);
}

FRect ButtonRect(const ButtonDef& button) {
    constexpr float gridX = 21.0f;
    constexpr float gridY = 361.0f;
    constexpr float cellW = 102.5f;
    constexpr float rowH = 72.2f;
    return {
        gridX + button.column * cellW + 5.0f,
        gridY + button.row * rowH + 5.0f,
        button.span * cellW - 10.0f,
        rowH - 10.0f
    };
}

FRect TrafficRect(int hit) {
    float centerX = 337.0f;
    if (hit == HitMinimize) centerX = 372.0f;
    if (hit == HitDrag) centerX = 407.0f;
    return {centerX - 11.5f, 32.0f, 23.0f, 23.0f};
}

FRect ScrollTrackRect() {
    return {42.0f, 333.0f, 368.0f, 5.0f};
}

float HistoryContentWidth() {
    return std::max(368.0f, static_cast<float>(g_history.size()) * 154.0f);
}

float MaxHistoryOffset() {
    return std::max(0.0f, HistoryContentWidth() - 368.0f);
}

FRect ScrollThumbRect() {
    FRect track = ScrollTrackRect();
    float content = HistoryContentWidth();
    float thumbWidth = content <= track.w ? track.w : std::max(34.0f, track.w * track.w / content);
    float travel = track.w - thumbWidth;
    float maxOffset = MaxHistoryOffset();
    float x = track.x + (maxOffset > 0.0f ? (g_historyOffset / maxOffset) * travel : 0.0f);
    return {x, track.y, thumbWidth, track.h};
}

bool PointInRoundedShell(float x, float y) {
    if (x < 0 || y < 0 || x > kWidth || y > kHeight) return false;
    float cx = x < kShellRadius ? kShellRadius : (x > kWidth - kShellRadius ? kWidth - kShellRadius : x);
    float cy = y < kShellRadius ? kShellRadius : (y > kHeight - kShellRadius ? kHeight - kShellRadius : y);
    float dx = x - cx;
    float dy = y - cy;
    return dx * dx + dy * dy <= kShellRadius * kShellRadius + 0.5f;
}

int HitTest(float x, float y) {
    for (int hit : {HitClose, HitMinimize, HitDrag}) {
        if (TrafficRect(hit).Contains(x, y)) return hit;
    }
    if (FRect{340.0f, 255.0f, 70.0f, 24.0f}.Contains(x, y)) return HitClearHistory;
    if (!g_history.empty()) {
        if (ScrollThumbRect().Contains(x, y)) return HitScrollThumb;
        FRect track = ScrollTrackRect();
        FRect expanded{track.x, track.y - 3.0f, track.w, track.h + 6.0f};
        if (expanded.Contains(x, y)) return HitScrollTrack;
    }
    for (int i = 0; i < static_cast<int>(std::size(kButtons)); ++i) {
        if (ButtonRect(kButtons[i]).Contains(x, y)) return i;
    }
    return HitNone;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

void AppendUtf8(std::string& output, unsigned int codePoint) {
    if (codePoint <= 0x7F) {
        output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    return -1;
}

std::string ParseJsonString(const std::string& json, size_t& position) {
    std::string result;
    if (position >= json.size() || json[position] != '"') return result;
    ++position;
    while (position < json.size()) {
        char ch = json[position++];
        if (ch == '"') break;
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (position >= json.size()) break;
        char escaped = json[position++];
        switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                if (position + 4 <= json.size()) {
                    unsigned int code = 0;
                    bool valid = true;
                    for (int i = 0; i < 4; ++i) {
                        int value = HexValue(json[position + i]);
                        if (value < 0) { valid = false; break; }
                        code = code * 16 + static_cast<unsigned int>(value);
                    }
                    if (valid) {
                        AppendUtf8(result, code);
                        position += 4;
                    }
                }
                break;
            }
            default: result.push_back(escaped); break;
        }
    }
    return result;
}

std::string JsonEscape(const std::wstring& value) {
    std::string utf8 = WideToUtf8(value);
    std::string result;
    result.reserve(utf8.size() + 8);
    for (unsigned char ch : utf8) {
        switch (ch) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(static_cast<char>(ch)); break;
        }
    }
    return result;
}

std::wstring HistoryPath() {
    wchar_t localAppData[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                nullptr, SHGFP_TYPE_CURRENT, localAppData))) return {};
    std::wstring directory = std::wstring(localAppData) + L"\\AppleGlassCalculator";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory + L"\\history.json";
}

std::string ReadAllBytes(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(file);
        return {};
    }
    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) return {};
    data.resize(read);
    return data;
}

void LoadHistory() {
    g_history.clear();
    std::string json = ReadAllBytes(HistoryPath());
    size_t position = 0;
    while (g_history.size() < kMaxHistory) {
        size_t expressionKey = json.find("\"Expression\"", position);
        if (expressionKey == std::string::npos) break;
        size_t expressionQuote = json.find('"', json.find(':', expressionKey) + 1);
        if (expressionQuote == std::string::npos) break;
        position = expressionQuote;
        std::string expression = ParseJsonString(json, position);
        size_t resultKey = json.find("\"Result\"", position);
        if (resultKey == std::string::npos) break;
        size_t resultQuote = json.find('"', json.find(':', resultKey) + 1);
        if (resultQuote == std::string::npos) break;
        position = resultQuote;
        std::string result = ParseJsonString(json, position);
        g_history.push_back({Utf8ToWide(expression), Utf8ToWide(result)});
    }
}

void SaveHistory() {
    std::wstring path = HistoryPath();
    if (path.empty()) return;
    std::string json = "[\n";
    for (size_t i = 0; i < g_history.size() && i < kMaxHistory; ++i) {
        json += "  {\"Expression\":\"" + JsonEscape(g_history[i].expression) +
                "\",\"Result\":\"" + JsonEscape(g_history[i].result) + "\"}";
        if (i + 1 < g_history.size() && i + 1 < kMaxHistory) json += ',';
        json += '\n';
    }
    json += "]\n";
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    BOOL ok = WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    if (ok && written == json.size()) {
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } else {
        DeleteFileW(temp.c_str());
    }
}

std::wstring GroupInteger(const std::wstring& integerPart) {
    bool negative = !integerPart.empty() && integerPart[0] == L'-';
    size_t begin = negative ? 1 : 0;
    std::wstring digits = integerPart.substr(begin);
    std::wstring grouped;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) grouped.push_back(L',');
        grouped.push_back(digits[i]);
    }
    return negative ? L"-" + grouped : grouped;
}

std::wstring FormatInput(const std::wstring& raw) {
    size_t dot = raw.find(L'.');
    if (dot == std::wstring::npos) return GroupInteger(raw);
    return GroupInteger(raw.substr(0, dot)) + raw.substr(dot);
}

std::wstring FormatNumber(double value) {
    if (!std::isfinite(value)) return L"数值错误";
    if (std::fabs(value) < 1e-18L) value = 0.0L;
    wchar_t buffer[160]{};
    std::swprintf(buffer, std::size(buffer), L"%.15g", value);
    std::wstring result = buffer;
    if (result.find_first_of(L"eE") != std::wstring::npos) return result;
    size_t dot = result.find(L'.');
    if (dot == std::wstring::npos) return GroupInteger(result);
    return GroupInteger(result.substr(0, dot)) + result.substr(dot);
}

bool ReadInput(double& value) {
    wchar_t* end = nullptr;
    value = std::wcstod(g_input.c_str(), &end);
    return end && end != g_input.c_str();
}

std::wstring OperatorSymbol(const std::wstring& operation) {
    if (operation == L"*") return L"×";
    if (operation == L"/") return L"÷";
    if (operation == L"-") return L"−";
    return operation;
}

void ClearAll() {
    g_input = L"0";
    g_display = L"0";
    g_expression.clear();
    g_pendingOperator.clear();
    g_lastOperator.clear();
    g_hasAccumulator = false;
    g_hasLastOperand = false;
    g_startNewInput = true;
    g_justEvaluated = false;
    g_hasError = false;
}

void RecoverError() {
    if (g_hasError) ClearAll();
}

void ShowError(const std::wstring& message) {
    g_display = message;
    g_expression.clear();
    g_pendingOperator.clear();
    g_hasAccumulator = false;
    g_startNewInput = true;
    g_justEvaluated = false;
    g_hasError = true;
}

bool Calculate(double left, double right, const std::wstring& operation, double& result) {
    if (operation == L"/" && std::fabs(right) < 1e-24L) {
        ShowError(L"无法除以零");
        return false;
    }
    if (operation == L"+") result = left + right;
    else if (operation == L"-") result = left - right;
    else if (operation == L"*") result = left * right;
    else if (operation == L"/") result = left / right;
    else return false;
    if (!std::isfinite(result)) {
        ShowError(L"数值超出范围");
        return false;
    }
    return true;
}

void ResetForFreshInput() {
    g_input = L"0";
    g_expression.clear();
    g_pendingOperator.clear();
    g_hasAccumulator = false;
    g_justEvaluated = false;
    g_startNewInput = true;
}

void InputDigit(wchar_t digit) {
    RecoverError();
    if (g_justEvaluated) ResetForFreshInput();
    if (g_startNewInput) {
        g_input.assign(1, digit);
        g_startNewInput = false;
    } else {
        int digits = static_cast<int>(std::count_if(g_input.begin(), g_input.end(), iswdigit));
        if (digits < 18) g_input = g_input == L"0" ? std::wstring(1, digit) : g_input + digit;
    }
    g_display = FormatInput(g_input);
}

void InputDecimal() {
    RecoverError();
    if (g_justEvaluated) ResetForFreshInput();
    if (g_startNewInput) {
        g_input = L"0.";
        g_startNewInput = false;
    } else if (g_input.find(L'.') == std::wstring::npos) {
        g_input += L'.';
    }
    g_display = FormatInput(g_input);
}

void SetOperator(const std::wstring& operation) {
    RecoverError();
    double current = 0.0;
    if (!ReadInput(current)) return;
    if (!g_pendingOperator.empty() && !g_startNewInput) {
        double chained = 0.0;
        if (!Calculate(g_hasAccumulator ? g_accumulator : current, current, g_pendingOperator, chained)) return;
        g_accumulator = chained;
        g_hasAccumulator = true;
        g_input = FormatNumber(chained);
        g_input.erase(std::remove(g_input.begin(), g_input.end(), L','), g_input.end());
        g_display = FormatNumber(chained);
    } else if (!g_hasAccumulator || g_justEvaluated) {
        g_accumulator = current;
        g_hasAccumulator = true;
    }
    g_pendingOperator = operation;
    g_expression = FormatNumber(g_accumulator) + L" " + OperatorSymbol(operation);
    g_startNewInput = true;
    g_justEvaluated = false;
}

void AddHistory(const std::wstring& expression, const std::wstring& result) {
    g_history.insert(g_history.begin(), {expression, result});
    if (g_history.size() > kMaxHistory) g_history.resize(kMaxHistory);
    g_historyOffset = 0.0f;
    SaveHistory();
}

void Evaluate() {
    RecoverError();
    double current = 0.0;
    if (!ReadInput(current)) return;
    std::wstring operation;
    double left = 0.0;
    double right = 0.0;
    if (!g_pendingOperator.empty()) {
        operation = g_pendingOperator;
        left = g_hasAccumulator ? g_accumulator : current;
        right = current;
        g_lastOperator = operation;
        g_lastOperand = right;
        g_hasLastOperand = true;
    } else if (g_justEvaluated && !g_lastOperator.empty() && g_hasLastOperand) {
        operation = g_lastOperator;
        left = current;
        right = g_lastOperand;
    } else {
        return;
    }
    double result = 0.0;
    if (!Calculate(left, right, operation, result)) return;
    std::wstring historyExpression = FormatNumber(left) + L" " + OperatorSymbol(operation) + L" " + FormatNumber(right);
    g_expression = historyExpression + L" =";
    g_display = FormatNumber(result);
    g_input = g_display;
    g_input.erase(std::remove(g_input.begin(), g_input.end(), L','), g_input.end());
    g_pendingOperator.clear();
    g_hasAccumulator = false;
    g_startNewInput = true;
    g_justEvaluated = true;
    AddHistory(historyExpression, g_display);
}

void Backspace() {
    if (g_hasError) {
        ClearAll();
        return;
    }
    if (g_startNewInput || g_justEvaluated) return;
    if (g_input.size() > 1) g_input.pop_back();
    else g_input = L"0";
    if (g_input == L"-") g_input = L"0";
    g_display = FormatInput(g_input);
}

void ClearEntry() {
    g_input = L"0";
    g_display = L"0";
    g_startNewInput = true;
    g_justEvaluated = false;
    g_hasError = false;
}

void HandleAction(const std::wstring& action) {
    if (action.size() == 1 && action[0] >= L'0' && action[0] <= L'9') InputDigit(action[0]);
    else if (action == L".") InputDecimal();
    else if (action == L"+" || action == L"-" || action == L"*" || action == L"/") SetOperator(action);
    else if (action == L"=") Evaluate();
    else if (action == L"Back") Backspace();
    else if (action == L"CE") ClearEntry();
    else if (action == L"C") ClearAll();
    std::wstring accessibleTitle = L"Apple Glass Calculator — " + g_display;
    SetWindowTextW(g_window, accessibleTitle.c_str());
}

void DrawRadialGlow(Graphics& graphics, const RectF& ellipseRect, Color center, Color edge) {
    GraphicsPath ellipse;
    ellipse.AddEllipse(ellipseRect);
    PathGradientBrush glow(&ellipse);
    glow.SetCenterColor(center);
    Color surround[] = {edge};
    INT count = 1;
    glow.SetSurroundColors(surround, &count);
    graphics.FillPath(&glow, &ellipse);
}

void DrawShell(Graphics& graphics) {
    const FRect shell{0.6f, 0.6f, kWidth - 1.2f, kHeight - 1.2f};
    GraphicsPath shellPath;
    AddRoundedRect(shellPath, shell, kShellRadius);
    const Color baseColors[] = {
        Color(238, 127, 143, 168),
        Color(232, 113, 118, 134),
        Color(235, 151, 111, 99)
    };
    const REAL baseStops[] = {0.0f, 0.56f, 1.0f};
    LinearGradientBrush base(PointF(0, 0), PointF(kWidth, kHeight), baseColors[0], baseColors[2]);
    base.SetInterpolationColors(baseColors, baseStops, 3);
    graphics.FillPath(&base, &shellPath);

    GraphicsState state = graphics.Save();
    graphics.SetClip(&shellPath);
    DrawRadialGlow(graphics, RectF(-235.0f, -185.0f, 430.0f, 500.0f),
                   Color(92, 169, 203, 255), Color(0, 159, 195, 255));
    DrawRadialGlow(graphics, RectF(197.0f, 313.0f, 440.0f, 600.0f),
                   Color(112, 255, 164, 100), Color(0, 255, 154, 92));
    const Color sheenColors[] = {
        Color(66, 255, 255, 255), Color(16, 218, 234, 255),
        Color(0, 255, 255, 255), Color(48, 255, 210, 182)
    };
    const REAL sheenStops[] = {0.0f, 0.28f, 0.58f, 1.0f};
    LinearGradientBrush sheen(PointF(0, 0), PointF(kWidth, kHeight), sheenColors[0], sheenColors[3]);
    sheen.SetInterpolationColors(sheenColors, sheenStops, 4);
    graphics.FillPath(&sheen, &shellPath);
    graphics.Restore(state);

    Pen outer(Color(225, 255, 255, 255), 1.25f);
    graphics.DrawPath(&outer, &shellPath);
    FRect innerRect{2.0f, 2.0f, kWidth - 4.0f, kHeight - 4.0f};
    const Color borderColors[] = {Color(235, 255, 255, 255), Color(90, 215, 232, 255), Color(185, 255, 224, 205)};
    const REAL borderStops[] = {0.0f, 0.5f, 1.0f};
    LinearGradientBrush border(PointF(0, 0), PointF(kWidth, kHeight), borderColors[0], borderColors[2]);
    border.SetInterpolationColors(borderColors, borderStops, 3);
    Pen inner(&border, 1.15f);
    StrokeRounded(graphics, innerRect, 36.0f, inner);
}

void DrawPanel(Graphics& graphics, const FRect& rect, float radius) {
    const Color colors[] = {
        Color(90, 255, 255, 255), Color(38, 232, 241, 255),
        Color(18, 255, 255, 255), Color(54, 255, 217, 197)
    };
    const REAL stops[] = {0.0f, 0.30f, 0.60f, 1.0f};
    FillGradientRounded(graphics, rect, radius, colors, stops, 4);
    const Color borderColors[] = {Color(235, 255, 255, 255), Color(110, 216, 232, 255), Color(200, 255, 228, 208)};
    const REAL borderStops[] = {0.0f, 0.55f, 1.0f};
    LinearGradientBrush border(PointF(rect.x, rect.y), PointF(rect.x + rect.w, rect.y + rect.h), borderColors[0], borderColors[2]);
    border.SetInterpolationColors(borderColors, borderStops, 3);
    Pen pen(&border, 1.05f);
    StrokeRounded(graphics, rect, radius, pen);
}

void DrawTrafficButton(Graphics& graphics, int hit, Color baseColor) {
    FRect rect = TrafficRect(hit);
    SolidBrush shadow(Color(55, 30, 25, 28));
    graphics.FillEllipse(&shadow, rect.x + 1.5f, rect.y + 3.0f, rect.w, rect.h);
    SolidBrush fill(baseColor);
    graphics.FillEllipse(&fill, rect.x, rect.y, rect.w, rect.h);
    Pen border(Color(235, 255, 255, 255), 1.0f);
    graphics.DrawEllipse(&border, rect.x, rect.y, rect.w, rect.h);
    SolidBrush shine(Color(g_hover == hit ? 55 : 32, 255, 255, 255));
    graphics.FillEllipse(&shine, rect.x + 3.0f, rect.y + 2.5f, rect.w - 6.0f, rect.h * 0.45f);
}

void DrawHistory(Graphics& graphics) {
    const FRect panel{26.0f, 248.0f, 400.0f, 96.0f};
    DrawPanel(graphics, panel, 22.0f);
    DrawTextLine(graphics, L"历史", {42.0f, 255.0f, 100.0f, 24.0f}, 13.5f, FontStyleRegular,
                 Color(205, 224, 229, 238));
    DrawTextLine(graphics, L"清空", {340.0f, 255.0f, 70.0f, 24.0f}, 13.0f, FontStyleRegular,
                 g_hover == HitClearHistory ? Color(255, 255, 255, 255) : Color(205, 224, 229, 238),
                 StringAlignmentFar);
    if (g_history.empty()) {
        DrawTextLine(graphics, L"完成计算后，记录会出现在这里", {42.0f, 280.0f, 340.0f, 38.0f},
                     12.5f, FontStyleRegular, Color(145, 210, 216, 228));
        return;
    }

    GraphicsState state = graphics.Save();
    RectF clip(42.0f, 280.0f, 368.0f, 49.0f);
    graphics.SetClip(clip);
    float x = 42.0f - g_historyOffset;
    for (const auto& item : g_history) {
        DrawTextLine(graphics, item.expression, {x, 281.0f, 130.0f, 19.0f}, 11.5f,
                     FontStyleRegular, Color(205, 224, 229, 238));
        DrawTextLine(graphics, item.result, {x, 300.0f, 130.0f, 22.0f}, 13.0f,
                     FontStyleBold, Color(245, 247, 248, 252));
        Pen separator(Color(42, 255, 255, 255), 1.0f);
        graphics.DrawLine(&separator, x + 141.0f, 284.0f, x + 141.0f, 319.0f);
        x += 154.0f;
    }
    graphics.Restore(state);

    FRect track = ScrollTrackRect();
    SolidBrush trackBrush(Color(38, 255, 255, 255));
    FillRounded(graphics, track, 2.5f, trackBrush);
    FRect thumb = ScrollThumbRect();
    SolidBrush thumbBrush(Color(g_hover == HitScrollThumb || g_draggingScroll ? 225 : 160, 255, 255, 255));
    FillRounded(graphics, thumb, 2.5f, thumbBrush);
}

void DrawButton(Graphics& graphics, int index) {
    const ButtonDef& button = kButtons[index];
    FRect rect = ButtonRect(button);
    FRect shadowRect{rect.x + 1.0f, rect.y + 4.0f, rect.w, rect.h};
    SolidBrush shadow(Color(52, 15, 23, 34));
    FillRounded(graphics, shadowRect, 20.0f, shadow);

    if (button.isOperator) {
        const Color colors[] = {Color(240, 255, 214, 159), Color(235, 249, 165, 77), Color(245, 234, 123, 40)};
        const REAL stops[] = {0.0f, 0.45f, 1.0f};
        FillGradientRounded(graphics, rect, 20.0f, colors, stops, 3);
    } else {
        const Color colors[] = {Color(105, 247, 250, 255), Color(48, 228, 236, 249),
                                Color(28, 255, 255, 255), Color(58, 255, 220, 203)};
        const REAL stops[] = {0.0f, 0.38f, 0.66f, 1.0f};
        FillGradientRounded(graphics, rect, 20.0f, colors, stops, 4);
    }
    Pen outer(button.isOperator ? Color(235, 255, 231, 183) : Color(180, 255, 255, 255), 1.0f);
    StrokeRounded(graphics, rect, 20.0f, outer);
    FRect inner{rect.x + 1.2f, rect.y + 1.2f, rect.w - 2.4f, rect.h - 2.4f};
    Pen innerPen(Color(100, 255, 255, 255), 0.8f);
    StrokeRounded(graphics, inner, 18.8f, innerPen);

    if (g_hover == index || g_pressed == index) {
        SolidBrush overlay(Color(g_pressed == index ? 26 : 20, 255, 255, 255));
        FillRounded(graphics, rect, 20.0f, overlay);
    }
    Color textColor = button.isClear ? Color(255, 255, 125, 117) : Color(250, 247, 248, 252);
    float fontSize = 27.0f;
    if (std::wstring(button.label) == L"CE") fontSize = 21.0f;
    if (std::wstring(button.label) == L"←") fontSize = 24.0f;
    if (button.isOperator) fontSize = 31.0f;
    float pressedOffset = g_pressed == index ? 1.0f : 0.0f;
    DrawTextLine(graphics, button.label, {rect.x, rect.y + pressedOffset, rect.w, rect.h}, fontSize,
                 FontStyleRegular, textColor, StringAlignmentCenter);
}

void DrawInterface(Graphics& graphics) {
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    graphics.ScaleTransform(g_scale, g_scale);

    DrawShell(graphics);
    DrawTextLine(graphics, L"计算器", {26.0f, 24.0f, 240.0f, 42.0f}, 29.0f, FontStyleBold,
                 Color(255, 247, 248, 252));
    DrawTextLine(graphics, L"标准计算", {27.0f, 62.0f, 180.0f, 28.0f}, 15.0f, FontStyleRegular,
                 Color(200, 220, 226, 236));
    DrawTrafficButton(graphics, HitClose, Color(255, 255, 95, 87));
    DrawTrafficButton(graphics, HitMinimize, Color(255, 255, 159, 10));
    DrawTrafficButton(graphics, HitDrag, Color(255, 40, 200, 64));

    const FRect displayPanel{26.0f, 108.0f, 400.0f, 126.0f};
    DrawPanel(graphics, displayPanel, 24.0f);
    DrawTextLine(graphics, g_expression, {46.0f, 121.0f, 360.0f, 30.0f}, 17.0f, FontStyleRegular,
                 Color(205, 224, 229, 238), StringAlignmentFar);
    float resultSize = 47.0f;
    if (g_display.size() > 14) resultSize = 38.0f;
    if (g_display.size() > 19) resultSize = 30.0f;
    DrawTextLine(graphics, g_display, {46.0f, 149.0f, 360.0f, 70.0f}, resultSize, FontStyleBold,
                 Color(252, 247, 248, 252), StringAlignmentFar);

    DrawHistory(graphics);
    for (int i = 0; i < static_cast<int>(std::size(kButtons)); ++i) DrawButton(graphics, i);
}

void RenderWindow() {
    if (!g_window || IsIconic(g_window)) return;
    int pixelWidth = std::max(1, static_cast<int>(std::lround(kWidth * g_scale)));
    int pixelHeight = std::max(1, static_cast<int>(std::lround(kHeight * g_scale)));
    Bitmap bitmap(pixelWidth, pixelHeight, PixelFormat32bppPARGB);
    Graphics graphics(&bitmap);
    graphics.SetCompositingMode(CompositingModeSourceCopy);
    graphics.Clear(Color(0, 0, 0, 0));
    graphics.SetCompositingMode(CompositingModeSourceOver);
    DrawInterface(graphics);
    graphics.Flush(FlushIntentionSync);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = pixelWidth;
    info.bmiHeader.biHeight = -pixelHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(memory, dib);

    Rect lockRect(0, 0, pixelWidth, pixelHeight);
    BitmapData data{};
    if (bitmap.LockBits(&lockRect, ImageLockModeRead, PixelFormat32bppPARGB, &data) == Ok) {
        auto* destination = static_cast<unsigned char*>(bits);
        auto* source = static_cast<unsigned char*>(data.Scan0);
        int sourceStride = data.Stride;
        for (int y = 0; y < pixelHeight; ++y) {
            const unsigned char* row = sourceStride >= 0
                ? source + y * sourceStride
                : source + (pixelHeight - 1 - y) * (-sourceStride);
            std::memcpy(destination + y * pixelWidth * 4, row, static_cast<size_t>(pixelWidth) * 4);
        }
        bitmap.UnlockBits(&data);
    }

    RECT windowRect{};
    GetWindowRect(g_window, &windowRect);
    POINT destinationPoint{windowRect.left, windowRect.top};
    SIZE size{pixelWidth, pixelHeight};
    POINT sourcePoint{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_window, screen, &destinationPoint, &size, memory, &sourcePoint,
                        0, &blend, ULW_ALPHA);
    SelectObject(memory, old);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

void ResizeForDpi(UINT dpi, const RECT* suggested = nullptr) {
    g_scale = std::max(1.0f, static_cast<float>(dpi) / 96.0f);
    int width = static_cast<int>(std::lround(kWidth * g_scale));
    int height = static_cast<int>(std::lround(kHeight * g_scale));
    int x = suggested ? suggested->left : 0;
    int y = suggested ? suggested->top : 0;
    if (!suggested) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + (work.right - work.left - width) / 2;
        y = work.top + (work.bottom - work.top - height) / 2;
    }
    SetWindowPos(g_window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    RenderWindow();
}

void BeginWindowDrag() {
    ReleaseCapture();
    SendMessageW(g_window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void UpdateHistoryFromThumb(float logicalX) {
    FRect track = ScrollTrackRect();
    FRect thumb = ScrollThumbRect();
    float travel = track.w - thumb.w;
    if (travel <= 0.0f) {
        g_historyOffset = 0.0f;
        return;
    }
    float thumbX = std::clamp(logicalX - g_scrollGrab, track.x, track.x + travel);
    g_historyOffset = (thumbX - track.x) / travel * MaxHistoryOffset();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window, &point);
            float x = point.x / g_scale;
            float y = point.y / g_scale;
            return PointInRoundedShell(x, y) ? HTCLIENT : HTTRANSPARENT;
        }
        case WM_DPICHANGED: {
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            ResizeForDpi(HIWORD(wParam), suggested);
            return 0;
        }
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            RenderWindow();
            return 0;
        case WM_MOUSEMOVE: {
            float x = GET_X_LPARAM(lParam) / g_scale;
            float y = GET_Y_LPARAM(lParam) / g_scale;
            if (g_draggingScroll) {
                UpdateHistoryFromThumb(x);
                RenderWindow();
                return 0;
            }
            int hit = HitTest(x, y);
            if (hit != g_hover) {
                g_hover = hit;
                RenderWindow();
            }
            if (!g_trackingMouse) {
                TRACKMOUSEEVENT event{sizeof(event), TME_LEAVE, window, 0};
                TrackMouseEvent(&event);
                g_trackingMouse = true;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_trackingMouse = false;
            g_hover = HitNone;
            if (g_pressed == HitNone) RenderWindow();
            return 0;
        case WM_LBUTTONDOWN: {
            float x = GET_X_LPARAM(lParam) / g_scale;
            float y = GET_Y_LPARAM(lParam) / g_scale;
            int hit = HitTest(x, y);
            if (hit == HitDrag) {
                BeginWindowDrag();
                return 0;
            }
            if (hit == HitScrollThumb) {
                FRect thumb = ScrollThumbRect();
                g_draggingScroll = true;
                g_scrollGrab = x - thumb.x;
                SetCapture(window);
                RenderWindow();
                return 0;
            }
            if (hit == HitScrollTrack) {
                FRect thumb = ScrollThumbRect();
                g_scrollGrab = thumb.w / 2.0f;
                UpdateHistoryFromThumb(x);
                RenderWindow();
                return 0;
            }
            if (hit != HitNone) {
                g_pressed = hit;
                SetCapture(window);
                RenderWindow();
                return 0;
            }
            if (y < 96.0f) {
                BeginWindowDrag();
                return 0;
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            float x = GET_X_LPARAM(lParam) / g_scale;
            float y = GET_Y_LPARAM(lParam) / g_scale;
            int hit = HitTest(x, y);
            if (g_draggingScroll) {
                g_draggingScroll = false;
                ReleaseCapture();
                RenderWindow();
                return 0;
            }
            int pressed = g_pressed;
            g_pressed = HitNone;
            ReleaseCapture();
            if (pressed == hit) {
                if (pressed == HitClose) {
                    PostMessageW(window, WM_CLOSE, 0, 0);
                    return 0;
                }
                if (pressed == HitMinimize) {
                    ShowWindow(window, SW_MINIMIZE);
                    return 0;
                }
                if (pressed == HitClearHistory) {
                    g_history.clear();
                    g_historyOffset = 0.0f;
                    SaveHistory();
                }
                if (pressed >= 0 && pressed < static_cast<int>(std::size(kButtons))) {
                    HandleAction(kButtons[pressed].action);
                }
            }
            RenderWindow();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window, &point);
            float x = point.x / g_scale;
            float y = point.y / g_scale;
            if (FRect{26.0f, 248.0f, 400.0f, 96.0f}.Contains(x, y)) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                g_historyOffset = std::clamp(g_historyOffset - delta * 0.45f, 0.0f, MaxHistoryOffset());
                RenderWindow();
            }
            return 0;
        }
        case WM_CHAR: {
            wchar_t ch = static_cast<wchar_t>(wParam);
            if (ch >= L'0' && ch <= L'9') HandleAction(std::wstring(1, ch));
            else if (ch == L'+' || ch == L'-' || ch == L'*' || ch == L'/') HandleAction(std::wstring(1, ch));
            else if (ch == L'.' || ch == L',') HandleAction(L".");
            else if (ch == L'=' || ch == L'\r') HandleAction(L"=");
            else if (ch == L'\b') HandleAction(L"Back");
            else if (ch == 27) HandleAction(L"C");
            else return 0;
            RenderWindow();
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_DELETE) {
                HandleAction(L"CE");
                RenderWindow();
                return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE) RenderWindow();
            return 0;
        case WM_CLOSE:
            SaveHistory();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    std::setlocale(LC_NUMERIC, "C");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput startupInput;
    if (GdiplusStartup(&g_gdiplusToken, &startupInput, nullptr) != Ok) return 1;
    LoadHistory();

    const wchar_t* className = L"AppleGlassCalculator.Native.Window";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1));
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass)) {
        GdiplusShutdown(g_gdiplusToken);
        return 2;
    }

    UINT dpi = GetDpiForSystem();
    g_scale = std::max(1.0f, static_cast<float>(dpi) / 96.0f);
    int width = static_cast<int>(std::lround(kWidth * g_scale));
    int height = static_cast<int>(std::lround(kHeight * g_scale));
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + (work.right - work.left - width) / 2;
    int y = work.top + (work.bottom - work.top - height) / 2;
    g_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, className, L"Apple Glass Calculator",
                               WS_POPUP, x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        GdiplusShutdown(g_gdiplusToken);
        return 3;
    }
    ShowWindow(g_window, showCommand == 0 ? SW_SHOWNORMAL : showCommand);
    RenderWindow();
    SetForegroundWindow(g_window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(message.wParam);
}
