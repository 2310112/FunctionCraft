// App.h — FunctionCraft 游戏界面状态与主循环入口
// 平台外壳（main.cpp）每帧调用 fcg::ui::frame(app)；
// 具体页面绘制分散在 MenuView.cpp / LevelSelect.cpp / GameView.cpp / ResultView.cpp / PlotView.cpp。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Constraint.h"
#include "core/Expr.h"
#include "core/Level.h"
#include "core/Scoring.h"
#include "imgui.h"

namespace fcg {
namespace ui {

enum class Screen { Menu, LevelSelect, Game };
enum class Mode { Level, Endless };

// 关卡 + 玩家进度
struct LevelEntry {
    LevelDef def;
    char bestGrade = 0;  // 0 = 未通关，否则 'S'..'D'
    bool cleared = false;
    int attempts = 0;
};

struct App {
    // ---------------- 字体/主题 ----------------
    bool fontsReady = false;

    // ---------------- 关卡数据 ----------------
    std::vector<LevelEntry> levels;
    bool usingFallbackLevels = false;
    std::string loadError;

    // ---------------- 导航 ----------------
    Screen screen = Screen::Menu;
    Mode mode = Mode::Level;
    bool timeAttack = false;  // 限时模式开关（默认关）
    int hoveredLevel = -1;

    // ---------------- 当前对局 ----------------
    bool endless = false;
    int endlessIndex = 1;      // 无尽模式当前关序号（从 1 开始）
    uint64_t endlessSeed = 0;  // 无尽模式随机种子
    int endlessBest = 0;       // 已通过的最高关数
    int endlessAttempts = 0;   // 无尽模式提交次数
    int totalSubmissions = 0;  // 全部提交次数（存档统计）
    int levelIndex = 0;        // 关卡模式下 levels 的下标
    LevelDef level;            // 当前关卡
    double levelStart = 0.0;   // ImGui::GetTime() 时间戳
    bool timeUp = false;       // 限时已耗尽

    // ---------------- 表达式输入 ----------------
    std::string expr;      // 内部 ASCII 表达式（sin(x) 等）
    int cursor = 0;        // 光标（字节下标）
    bool exprValid = false;
    std::string exprError;  // 中文错误提示
    ExprP ast;              // 解析后的 AST（有效时非空）

    // ---------------- 提交与评分 ----------------
    bool hasResult = false;  // 结果覆盖层是否显示
    int resultFrame = -1;    // 打开结果覆盖层的帧号（避免同帧回车重复触发）
    ScoreBreakdown result;
    std::vector<ConstraintReport> reports;
    bool correct = false;
    double submitElapsed = 0.0;

    // ---------------- 提示 ----------------
    bool hintOpen = false;        // 提示气泡（关卡 hint）
    bool referenceOpen = false;   // 是否显示参考解
    bool resetConfirmOpen = false;  // 重置进度确认框

    // ---------------- 轻提示（toast） ----------------
    std::string toast;
    double toastUntil = 0.0;
    bool toastError = false;

    // ---------------- 计时/帧 ----------------
    double now = 0.0;
    double dt = 0.0;
    int frameNo = 0;

    // ---------------- 生命周期 ----------------
    void init();
    void shutdown();
    void tick();   // 每帧：计时 + 状态更新
    void draw();   // 每帧：页面绘制

    // ---------------- 数据 ----------------
    void loadGameData();
    void loadProgress();
    void saveProgress() const;
    void resetProgress();
    LevelDef& currentLevel() { return level; }
    const LevelDef& currentLevel() const { return level; }
    bool hasNextLevel() const;

    // ---------------- 导航 ----------------
    void goMenu();
    void goLevelSelect();
    void startLevelMode();
    void startEndlessMode();
    void openLevel(int index);
    void openEndlessLevel(int index);
    void nextLevel();
    void retryLevel();
    void backFromGame();
    void requestQuit();

    // ---------------- 输入 ----------------
    void setExpr(const std::string& s);
    void insertText(const std::string& text);   // 在光标处插入（自动配对括号）
    void insertFunction(const std::string& fn); // 插入 "sin(" 并自动补 ")"
    void insertChar(char c);
    void backspace();
    void deleteForward();
    void moveCursor(int delta);
    void clearExpr();
    void refreshValidation();
    void handleGameKeyboard();  // 游戏界面键盘输入

    // ---------------- 提交 ----------------
    void submit();
    void forceTimeout();

    // ---------------- 辅助 ----------------
    double elapsed() const;
    double timeLimit() const;      // 秒；0 = 不限时
    double remainingTime() const;  // 秒；不限时返回 0
    std::string currentLevelTitle() const;
    void recordGrade(char grade);
    void toastMsg(bool error, const std::string& msg);
    void applyShotConfig();  // 处理 --scene/--level/--expr
    void regenerateEndlessLevel();
};

extern App* gActiveApp;

void frame(App& app);

// ---------------- 各页面绘制（分别在不同 .cpp） ----------------
void DrawMenu(App& app);
void DrawLevelSelect(App& app);
void DrawGame(App& app);
void DrawResultOverlay(App& app);
void DrawPlotCard(App& app, const ImVec2& size);   // 右侧函数图像卡片
void DrawHintModal(App& app);                      // 提示弹窗（关卡提示）
void DrawResetConfirm(App& app);                   // 重置进度确认框
bool UiIsCompact();                                // 当前是否处于小窗口紧凑布局
void DrawMenuCanvas(App& app, const ImVec2& min, const ImVec2& max);  // 菜单背景装饰曲线

}  // namespace ui
}  // namespace fcg
