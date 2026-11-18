// App.cpp — 游戏界面状态机：数据加载、导航、输入、提交、主循环入口
#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include <windows.h>

#include "core/EndlessGen.h"
#include "core/Json.h"
#include "core/Level.h"
#include "LevelUi.h"
#include "core/Parser.h"
#include "core/Scoring.h"
#include "ShotConfig.h"
#include "Theme.h"
#include "imgui.h"

namespace fcg {
namespace ui {

App* gActiveApp = nullptr;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultTimeLimit = 180.0;

bool IsStubDetail(const std::string& s) {
    return s == "未实现" || s == "未实现。";
}

}  // namespace

// ---------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------
void App::init() {
    gActiveApp = this;
    now = ImGui::GetTime();
    frameNo = 0;

    // 输入完全由本模块接管（避免 ImGui 键盘导航抢走回车/方向键）
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;


    loadGameData();
    loadProgress();

    // 标题字体需要的额外文本（关卡标题）
    std::string extra;
    for (const auto& e : levels) extra += e.def.title;
    extra += "第一章第二章第三章第四章第五章第六章基础组合进阶大师无尽试炼关卡选择模式挑战";

    FontSetupResult fr = SetupFonts(extra, std::string());
    fontsReady = fr.ok;
    ApplyTheme();

    refreshValidation();
    applyShotConfig();
}

void App::shutdown() {
    saveProgress();
    if (gActiveApp == this) gActiveApp = nullptr;
}

void App::tick() {
    now = ImGui::GetTime();
    dt = ImGui::GetIO().DeltaTime;
    if (dt < 0.0) dt = 0.0;
    ++frameNo;


    if (screen == Screen::Game && !hasResult && timeAttack && !timeUp) {
        double lim = timeLimit();
        if (lim > 0.0 && elapsed() >= lim) {
            timeUp = true;
            forceTimeout();
        }
    }
}

void App::draw() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::Bg);
    ImGui::Begin("##fcg_root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // 覆盖层打开时禁用底层交互（视觉上由半透明遮罩压暗）
    if (resetConfirmOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) resetConfirmOpen = false;
    bool overlayOpen =
        ((screen == Screen::Game) && (hasResult || hintOpen)) || resetConfirmOpen;
    if (overlayOpen) ImGui::BeginDisabled();
    switch (screen) {
        case Screen::Menu: DrawMenu(*this); break;
        case Screen::LevelSelect: DrawLevelSelect(*this); break;
        case Screen::Game: DrawGame(*this); break;
    }
    if (overlayOpen) ImGui::EndDisabled();

    if (hasResult) {
        // 结果覆盖层的键盘操作（避免打开当帧的同一个回车被二次消费）
        if (resultFrame != frameNo) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                hasResult = false;
                backFromGame();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
                hasResult = false;
                bool canAdvance = correct && (endless || hasNextLevel());
                if (canAdvance)
                    nextLevel();
                else
                    retryLevel();
            }
        }
    } else if (screen == Screen::LevelSelect) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) goMenu();
    } else if (screen == Screen::Game && hintOpen) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) hintOpen = false;
    }

    // 轻提示（toast）
    if (!toast.empty()) {
        double left = toastUntil - now;
        if (left <= 0.0) {
            toast.clear();
        } else {
            float alpha = static_cast<float>(Clamp(left / 0.5, 0.0, 1.0));
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 ts = CalcText(toast.c_str());
            float w = ts.x + 44.0f;
            float h = 46.0f;
            ImVec2 p0(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f,
                      vp->WorkPos.y + vp->WorkSize.y - 96.0f);
            ImVec2 p1(p0.x + w, p0.y + h);
            ImU32 bgc = AlphaU32(ToU32(col::Panel), 0.96f * alpha);
            fg->AddRectFilled(ImVec2(p0.x + 2, p0.y + 4), ImVec2(p1.x + 2, p1.y + 4),
                              AlphaU32(0xFF000000, 0.25f * alpha), 12.0f);
            fg->AddRectFilled(p0, p1, bgc, 12.0f);
            ImU32 line = toastError ? ToU32(col::Red) : ToU32(col::Cyan);
            fg->AddRectFilled(p0, ImVec2(p0.x + 4, p1.y), line, 2.0f);
            fg->AddText(gFontBody, gFontBody->FontSize,
                        ImVec2(p0.x + 22.0f, p0.y + (h - gFontBody->FontSize) * 0.5f),
                        AlphaU32(ToU32(col::Text), alpha), toast.c_str());
        }
    }

    // 覆盖层在根窗口内最后绘制，保证位于最上层且禁用底层交互
    if (screen == Screen::Game) {
        if (hasResult) {
            DrawResultOverlay(*this);
        } else if (hintOpen) {
            DrawHintModal(*this);
        }
    }
    if (resetConfirmOpen) DrawResetConfirm(*this);

    ImGui::End();
}

// ---------------------------------------------------------------
// 数据加载
// ---------------------------------------------------------------
void App::loadGameData() {
    levels.clear();
    std::string err;
    std::vector<LevelDef> defs = loadLevels("data/levels.json", &err);
    if (!defs.empty()) {
        for (auto& d : defs) {
            if (d.constraints.empty()) continue;
            LevelEntry e;
            e.def = std::move(d);
            levels.push_back(std::move(e));
        }
    }
    if (levels.empty()) {
        loadError = err.empty() ? std::string("未找到 data/levels.json") : err;
        levels = MakeFallbackLevels();
        usingFallbackLevels = true;
    } else {
        usingFallbackLevels = false;
        loadError.clear();
    }
}

bool App::hasNextLevel() const {
    if (endless) return true;
    return levelIndex + 1 < static_cast<int>(levels.size());
}

// ---------------------------------------------------------------
// 导航
// ---------------------------------------------------------------
void App::goMenu() {
    screen = Screen::Menu;
    hasResult = false;
    hintOpen = false;
    referenceOpen = false;
    endless = false;
    mode = Mode::Level;
}

void App::goLevelSelect() {
    screen = Screen::LevelSelect;
    hasResult = false;
    hintOpen = false;
    referenceOpen = false;
}

void App::startLevelMode() {
    mode = Mode::Level;
    endless = false;
    goLevelSelect();
}

void App::startEndlessMode() {
    mode = Mode::Endless;
    endless = true;
    endlessSeed = static_cast<uint64_t>(ImGui::GetTime() * 1000.0) ^ 0x9E3779B97F4A7C15ull;
    endlessIndex = 1;
    openEndlessLevel(1);
}

void App::openLevel(int index) {
    if (levels.empty()) return;
    index = Clamp(index, 0, static_cast<int>(levels.size()) - 1);
    levelIndex = index;
    endless = false;
    mode = Mode::Level;
    level = levels[index].def;
    levelStart = ImGui::GetTime();
    screen = Screen::Game;
    hasResult = false;
    hintOpen = false;
    referenceOpen = false;
    timeUp = false;
    setExpr("");
}

void App::regenerateEndlessLevel() {
    level = generateEndlessLevel(endlessIndex, endlessSeed);
    bool bad = level.constraints.empty() || level.title.empty() || level.reference.empty();
    if (bad) {
        std::vector<LevelDef> pool;
        pool.reserve(levels.size());
        for (const auto& e : levels) pool.push_back(e.def);
        level = MakeEndlessFallbackLevel(endlessIndex, pool);
    }
    if (level.title.empty()) level.title = "无尽试炼 · 第 " + std::to_string(endlessIndex) + " 关";
    level.timeLimit = 0.0;
}

void App::openEndlessLevel(int index) {
    endlessIndex = std::max(1, index);
    endless = true;
    mode = Mode::Endless;
    regenerateEndlessLevel();
    levelStart = ImGui::GetTime();
    screen = Screen::Game;
    hasResult = false;
    hintOpen = false;
    referenceOpen = false;
    timeUp = false;
    setExpr("");
}

void App::nextLevel() {
    if (endless) {
        hasResult = false;
        openEndlessLevel(endlessIndex + 1);
    } else if (hasNextLevel()) {
        hasResult = false;
        openLevel(levelIndex + 1);
    } else {
        hasResult = false;
        goLevelSelect();
        toastMsg(false, "已完成全部关卡！");
    }
}

void App::retryLevel() {
    hasResult = false;
    if (endless)
        openEndlessLevel(endlessIndex);
    else
        openLevel(levelIndex);
}

void App::backFromGame() {
    if (endless) {
        goMenu();
    } else {
        goLevelSelect();
    }
}

void App::requestQuit() {
    HWND hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
    if (hwnd) ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

// ---------------------------------------------------------------
// 输入
// ---------------------------------------------------------------
void App::setExpr(const std::string& s) {
    expr = s;
    cursor = static_cast<int>(expr.size());
    refreshValidation();
}

void App::refreshValidation() {
    exprValid = false;
    exprError.clear();
    ast = nullptr;
    if (cursor < 0) cursor = 0;
    if (cursor > static_cast<int>(expr.size())) cursor = static_cast<int>(expr.size());
    if (expr.empty()) {
        exprError = "输入一个表达式，例如 sin(x)";
        return;
    }
    ParseError pe;
    ExprP e = parseExpr(expr, &pe);
    if (!e) {
        exprError = pe.message.empty() ? "语法错误" : pe.message;
        return;
    }
    ast = e;
    exprValid = true;
}

void App::insertText(const std::string& text) {
    if (text.empty() || hasResult) return;
    int cur = Clamp(cursor, 0, static_cast<int>(expr.size()));
    std::string ins = text;
    bool autoParen = ins.back() == '(';
    if (autoParen) ins += ')';
    expr.insert(static_cast<size_t>(cur), ins);
    cursor = cur + static_cast<int>(ins.size()) - (autoParen ? 1 : 0);
    refreshValidation();
}

void App::insertFunction(const std::string& fn) {
    if (fn.empty()) return;
    std::string t = fn;
    if (t.back() != '(') t += '(';
    insertText(t);
}

void App::insertChar(char c) {
    if (c == ')') {
        // 若光标右侧是自动补全的右括号，则直接跳过
        if (cursor < static_cast<int>(expr.size()) && expr[static_cast<size_t>(cursor)] == ')') {
            ++cursor;
            return;
        }
    }
    insertText(std::string(1, c));
}

void App::backspace() {
    if (hasResult) return;
    if (cursor > 0 && cursor <= static_cast<int>(expr.size())) {
        expr.erase(static_cast<size_t>(cursor - 1), 1);
        --cursor;
        refreshValidation();
    }
}

void App::deleteForward() {
    if (hasResult) return;
    if (cursor >= 0 && cursor < static_cast<int>(expr.size())) {
        expr.erase(static_cast<size_t>(cursor), 1);
        refreshValidation();
    }
}

void App::moveCursor(int delta) {
    cursor = Clamp(cursor + delta, 0, static_cast<int>(expr.size()));
}

void App::clearExpr() {
    expr.clear();
    cursor = 0;
    refreshValidation();
}

void App::handleGameKeyboard() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        backFromGame();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        submit();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
        if (io.KeyCtrl)
            clearExpr();
        else
            backspace();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) deleteForward();
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) moveCursor(-1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) moveCursor(1);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) cursor = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) cursor = static_cast<int>(expr.size());

    if (io.KeyCtrl || io.KeyAlt || io.KeySuper) return;

    // 数字行：直接输入数字；Shift 映射常用符号
    static const ImGuiKey digitKeys[10] = {
        ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
        ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9};
    static const char shifted[10] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
    for (int i = 0; i < 10; ++i) {
        if (ImGui::IsKeyPressed(digitKeys[i], true)) {
            if (io.KeyShift) {
                char c = shifted[i];
                if (c == '^' || c == '*' || c == '(' || c == ')') insertChar(c);
            } else {
                insertChar(static_cast<char>('0' + i));
            }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Period, true)) insertChar('.');
    if (ImGui::IsKeyPressed(ImGuiKey_Minus, true)) insertChar('-');
    if (ImGui::IsKeyPressed(ImGuiKey_Slash, true)) insertChar('/');
    if (ImGui::IsKeyPressed(ImGuiKey_Equal, true) && io.KeyShift) insertChar('+');

    // 字母：直接输入（函数名 / 变量 x）
    static const ImGuiKey letterKeys[26] = {
        ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F, ImGuiKey_G,
        ImGuiKey_H, ImGuiKey_I, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L, ImGuiKey_M, ImGuiKey_N,
        ImGuiKey_O, ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R, ImGuiKey_S, ImGuiKey_T, ImGuiKey_U,
        ImGuiKey_V, ImGuiKey_W, ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
    for (int i = 0; i < 26; ++i) {
        if (ImGui::IsKeyPressed(letterKeys[i], true)) {
            insertChar(static_cast<char>('a' + i));
        }
    }
}

// ---------------------------------------------------------------
// 提交 / 评分
// ---------------------------------------------------------------
void App::submit() {
    if (hasResult) return;
    if (!exprValid || !ast) {
        toastMsg(true, expr.empty() ? "请先输入表达式" : "表达式有误，请先修正红色提示");
        return;
    }

    submitElapsed = elapsed();
    result = evaluateSubmission(ast, level, submitElapsed);

    reports.clear();
    for (const auto& r : result.reports) reports.push_back(r);
    bool fromScoring = !reports.empty() &&
                       reports.size() == static_cast<size_t>(level.constraints.size());
    if (!fromScoring) {
        reports.clear();
        for (const auto& c : level.constraints) {
            ConstraintReport r = checkConstraint(c, ast);
            if (r.label.empty()) r.label = ConstraintDesc(c);
            reports.push_back(r);
        }
    }

    bool allOk = !reports.empty();
    for (const auto& r : reports)
        if (!r.ok) allOk = false;

    correct = result.correct || allOk;

    // 评分模块尚未就绪时的可读兜底（集成后由 evaluateSubmission 覆盖）
    if (correct && result.total <= 0.0) {
        result.correct = true;
        result.precision = result.precision > 0 ? result.precision : 100.0;
        if (result.elegance <= 0.0) {
            int calls = ast ? funcCallCount(ast) : 0;
            result.elegance = Clamp(100.0 - calls * 14.0, 40.0, 100.0);
        }
        if (result.robustness <= 0.0) result.robustness = 92.0;
        if (result.speed <= 0.0) {
            double lim = timeLimit();
            if (lim > 0)
                result.speed = Clamp(100.0 * remainingTime() / lim, 0.0, 100.0);
            else
                result.speed = 100.0;
        }
        result.total = 0.35 * result.precision + 0.30 * result.elegance +
                       0.25 * result.robustness + 0.10 * result.speed;
    }
    if (correct && (result.grade == 0 || result.grade == 'D') && result.total >= 60.0) {
        result.grade = gradeOf(result.total);
        result.gradeLabel = gradeLabel(result.grade);
    }
    if (!correct) result.correct = false;

    // 记录进度
    if (!endless && !levels.empty()) {
        auto& entry = levels[static_cast<size_t>(levelIndex)];
        ++entry.attempts;
        if (correct) {
            entry.cleared = true;
            if (GradeBetter(result.grade, entry.bestGrade)) entry.bestGrade = result.grade;
            recordGrade(result.grade);
        }
    } else {
        ++endlessAttempts;
        if (correct) {
            endlessBest = std::max(endlessBest, endlessIndex);
            recordGrade(result.grade);
        }
    }

    ++totalSubmissions;
    hasResult = true;
    resultFrame = frameNo;
    hintOpen = false;
}

void App::forceTimeout() {
    hasResult = false;
    result = ScoreBreakdown();
    reports.clear();
    if (exprValid && ast) {
        submit();
        result.feedback.push_back("时间耗尽：已自动提交当前表达式");
    } else {
        result.correct = false;
        result.grade = 'D';
        result.total = 0.0;
        result.feedback.push_back("时间耗尽：未能在限定时间内提交合法表达式");
        for (const auto& c : level.constraints) {
            ConstraintReport r;
            r.kind = c.kind;
            r.label = ConstraintDesc(c);
            r.ok = false;
            r.detail = "超时未提交";
            reports.push_back(r);
        }
        correct = false;
        hasResult = true;
        resultFrame = frameNo;
        ++endlessAttempts;
        ++totalSubmissions;
    }
}

// ---------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------
double App::elapsed() const {
    double e = ImGui::GetTime() - levelStart;
    return e < 0.0 ? 0.0 : e;
}

double App::timeLimit() const {
    if (!timeAttack) return 0.0;
    if (level.timeLimit > 0.0) return level.timeLimit;
    return kDefaultTimeLimit;
}

double App::remainingTime() const {
    double lim = timeLimit();
    if (lim <= 0.0) return 0.0;
    double r = lim - elapsed();
    return r < 0.0 ? 0.0 : r;
}

std::string App::currentLevelTitle() const { return level.title; }

void App::recordGrade(char grade) {
    // 关卡模式进度在 submit() 中记录；这里处理无尽模式最高关数
    (void)grade;
}

void App::toastMsg(bool error, const std::string& msg) {
    toast = msg;
    toastError = error;
    toastUntil = ImGui::GetTime() + 2.8;
}

// ---------------------------------------------------------------
// 截图/调试场景
// ---------------------------------------------------------------
void App::applyShotConfig() {
    const ShotConfig& sc = g_shotConfig;
    if (sc.scene.empty()) return;

    if (sc.scene == "menu") {
        screen = Screen::Menu;
        return;
    }
    if (sc.scene == "levels") {
        mode = Mode::Level;
        endless = false;
        screen = Screen::LevelSelect;
        return;
    }
    if (sc.scene == "endless") {
        mode = Mode::Endless;
        endless = true;
        endlessSeed = 20241118ULL;
        endlessIndex = 3;
        openEndlessLevel(endlessIndex);
        std::string text = sc.expression;
        if (text.empty()) text = level.reference.empty() ? "sin(x)" : level.reference;
        setExpr(text);
        levelStart = ImGui::GetTime() - 37.0;
        return;
    }
    if (sc.scene == "play" || sc.scene == "result") {
        int idx = 0;
        if (!sc.levelId.empty()) {
            bool found = false;
            for (size_t i = 0; i < levels.size(); ++i) {
                if (levels[i].def.id == sc.levelId) {
                    idx = static_cast<int>(i);
                    found = true;
                    break;
                }
            }
            if (!found) {
                try {
                    int n = std::stoi(sc.levelId);
                    idx = Clamp(n - 1, 0, static_cast<int>(levels.size()) - 1);
                } catch (...) {
                    idx = 0;
                }
            }
        }
        if (!levels.empty()) {
            openLevel(idx);
        } else {
            screen = Screen::Game;
        }
        std::string text = sc.expression;
        if (text.empty()) {
            text = level.reference;
            if (text.empty()) text = "sin(x)";
        }
        setExpr(text);
        if (sc.scene == "play") {
            levelStart = ImGui::GetTime() - 23.0;
        } else {
            levelStart = ImGui::GetTime() - 42.0;
            submitElapsed = 42.0;
            submit();
        }
        return;
    }
}

// ---------------------------------------------------------------
// 进度存档（exe 同目录 save.json，使用 core Json）
// ---------------------------------------------------------------
namespace {

// exe 所在目录下的文件路径（避免依赖启动时的当前工作目录）
std::string ExeDirFile(const char* name) {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::string(name);
    std::wstring path(buf, n);
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        path = path.substr(0, slash + 1);
    else
        path.clear();
    std::wstring wname(name, name + std::strlen(name));
    path += wname;
    char out[MAX_PATH * 4] = {0};
    int len = ::WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), out,
                                    sizeof(out) - 1, nullptr, nullptr);
    if (len <= 0) return std::string(name);
    return std::string(out, static_cast<size_t>(len));
}

std::string SavePath() { return ExeDirFile("save.json"); }

}  // namespace

void App::loadProgress() {
    std::string err;
    Json doc = Json::parseFile(SavePath(), &err);
    if (!doc.isObject()) return;  // 无存档/损坏：使用空进度，不打扰玩家

    endlessBest = doc["endlessBest"].asInt(0);
    double seedNum = doc["endlessSeed"].asNumber(0.0);
    endlessSeed = static_cast<uint64_t>(seedNum);
    totalSubmissions = doc["totalSubmissions"].asInt(0);

    const Json& lvJson = doc["levels"];
    if (!lvJson.isObject()) return;
    for (const std::string& id : lvJson.keys()) {
        const Json& item = lvJson[id];
        if (!item.isObject()) continue;
        for (auto& entry : levels) {
            if (entry.def.id != id) continue;
            entry.cleared = item["cleared"].asBool(false);
            entry.attempts = item["attempts"].asInt(0);
            std::string grade = item["bestGrade"].asString();
            entry.bestGrade = grade.empty() ? 0 : grade[0];
            break;
        }
    }
}

void App::saveProgress() const {
    std::map<std::string, Json> root;
    root["version"] = Json(1.0);
    root["endlessBest"] = Json(static_cast<double>(endlessBest));
    root["endlessSeed"] = Json(static_cast<double>(endlessSeed));
    root["totalSubmissions"] = Json(static_cast<double>(totalSubmissions));

    std::map<std::string, Json> levelMap;
    for (const auto& entry : levels) {
        if (!entry.cleared && entry.attempts == 0) continue;
        std::map<std::string, Json> item;
        item["cleared"] = Json(entry.cleared);
        item["bestGrade"] =
            Json(entry.bestGrade ? std::string(1, entry.bestGrade) : std::string());
        item["attempts"] = Json(static_cast<double>(entry.attempts));
        levelMap[entry.def.id] = Json(item);
    }
    root["levels"] = Json(levelMap);

    std::string text = Json(root).dump(2);
    std::string path = SavePath();
    std::error_code ec;
    std::filesystem::path fsPath = std::filesystem::u8path(path);
    std::ofstream out(fsPath, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
}

void App::resetProgress() {
    for (auto& entry : levels) {
        entry.cleared = false;
        entry.bestGrade = 0;
        entry.attempts = 0;
    }
    endlessBest = 0;
    endlessAttempts = 0;
    totalSubmissions = 0;
    saveProgress();
    toastMsg(false, "进度已重置");
}

bool UiIsCompact() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    return vp && (vp->Size.x < 1180.0f || vp->Size.y < 740.0f);
}

void frame(App& app) {
    app.tick();
    app.draw();
}

}  // namespace ui
}  // namespace fcg
