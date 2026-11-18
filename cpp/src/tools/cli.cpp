// cli.cpp — 命令行工具：关卡自检、约束验证、生成器测试
// 用法见 PrintUsage()
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/Constraint.h"
#include "core/Domain.h"
#include "core/EndlessGen.h"
#include "core/Diff.h"
#include "core/Expr.h"
#include "core/Level.h"
#include "core/Limit.h"
#include "core/Numeric.h"
#include "core/Parser.h"
#include "core/Scoring.h"
#include "core/Simplify.h"

using namespace fcg;

namespace {

void PrintUsage() {
    std::printf(R"(函数工坊 命令行工具

  fcg_cli levels   <levels.json>              列出关卡
  fcg_cli verify   <levels.json>              用每关 reference 验证约束是否自洽
  fcg_cli check    <levels.json> <id> <expr>  用表达式检查某关
  fcg_cli endless  <seed> <count>             生成 count 关无尽关卡并自检
  fcg_cli eval     <expr> [x]                 求值
  fcg_cli parse    <expr>                     解析并打印 AST
  fcg_cli domain   <expr>                     打印定义域
  fcg_cli diff     <expr>                     求导
  fcg_cli limit    <expr> <x0>                求 x→x0 的极限
)");
}

ExprP MustParse(const std::string& s) {
    ParseError pe;
    ExprP e = parseExpr(s, &pe);
    if (!e) {
        std::printf("解析错误 (位置 %d): %s\n", pe.pos, pe.message.c_str());
        return nullptr;
    }
    return e;
}

int CmdLevels(const std::string& path) {
    std::string err;
    auto levels = loadLevels(path, &err);
    if (!err.empty()) {
        std::printf("加载失败: %s\n", err.c_str());
        return 1;
    }
    if (levels.empty()) {
        std::printf("没有关卡\n");
        return 1;
    }
    std::printf("共 %zu 关\n\n", levels.size());
    int chapter = -1;
    for (auto& lv : levels) {
        if (lv.chapter != chapter) {
            chapter = lv.chapter;
            std::printf("== 第 %d 章 ==\n", chapter);
        }
        std::printf("  [%s] %s  (%zu 条约束)\n", lv.id.c_str(), lv.title.c_str(),
                    lv.constraints.size());
    }
    return 0;
}

int CmdVerify(const std::string& path) {
    std::string err;
    auto levels = loadLevels(path, &err);
    if (!err.empty()) {
        std::printf("加载失败: %s\n", err.c_str());
        return 1;
    }
    int bad = 0;
    for (auto& lv : levels) {
        ExprP ref = MustParse(lv.reference);
        if (!ref) {
            std::printf("[%s] 参考解无法解析: %s\n", lv.id.c_str(), lv.reference.c_str());
            ++bad;
            continue;
        }
        std::vector<ConstraintReport> reports;
        bool ok = checkAllConstraints(ref, lv, &reports);
        if (!ok) {
            ++bad;
            std::printf("[%s] %s —— 参考解不满足约束:\n", lv.id.c_str(), lv.title.c_str());
            for (auto& r : reports) {
                if (!r.ok) std::printf("      ✗ %s: %s\n", r.label.c_str(), r.detail.c_str());
            }
        } else {
            std::printf("[%s] OK  参考解 %-24s 满足 %zu 条约束\n", lv.id.c_str(),
                        lv.reference.c_str(), lv.constraints.size());
        }
    }
    std::printf("\n%d/%zu 关通过\n", (int)levels.size() - bad, levels.size());
    return bad == 0 ? 0 : 1;
}

int CmdCheck(const std::string& path, const std::string& id, const std::string& expr) {
    std::string err;
    auto levels = loadLevels(path, &err);
    if (!err.empty()) {
        std::printf("加载失败: %s\n", err.c_str());
        return 1;
    }
    for (auto& lv : levels) {
        if (lv.id != id) continue;
        ExprP e = MustParse(expr);
        if (!e) return 1;
        auto sc = evaluateSubmission(e, lv, 0.0);
        std::printf("关卡 %s：%s\n", lv.id.c_str(), lv.title.c_str());
        std::printf("表达式：%s\n\n", e->toStr().c_str());
        for (auto& r : sc.reports) {
            std::printf("  [%s] %s%s\n", r.ok ? "✓" : "✗", r.label.c_str(),
                        r.ok ? "" : ("  —— " + r.detail).c_str());
        }
        std::printf("\n总分 %.1f  评级 %c  精确 %.1f 优雅 %.1f 鲁棒 %.1f 时间 %.1f\n",
                    sc.total, sc.grade, sc.precision, sc.elegance, sc.robustness, sc.speed);
        return sc.correct ? 0 : 1;
    }
    std::printf("找不到关卡 %s\n", id.c_str());
    return 1;
}

int CmdEndless(uint64_t seed, int count) {
    std::string err;
    std::printf("生成 %d 关（seed=%llu）\n\n", count, (unsigned long long)seed);
    for (int i = 1; i <= count; ++i) {
        LevelDef lv = generateEndlessLevel(i, seed);
        ExprP ref = MustParse(lv.reference);
        bool ok = false;
        std::string why = "参考解解析失败";
        if (ref) {
            std::vector<ConstraintReport> reports;
            ok = checkAllConstraints(ref, lv, &reports);
            if (!ok) {
                for (auto& r : reports)
                    if (!r.ok) {
                        why = r.label + ": " + r.detail;
                        break;
                    }
            }
        }
        std::printf("  %3d %-28s 约束=%zu 参考=%-22s %s\n", i, lv.title.c_str(),
                    lv.constraints.size(), lv.reference.c_str(), ok ? "OK" : ("失败: " + why).c_str());
    }
    return 0;
}

int CmdEval(const std::string& expr, bool hasX, double x) {
    ExprP e = MustParse(expr);
    if (!e) return 1;
    VarMap m;
    if (hasX) m["x"] = Cplx(x);
    auto r = evaluate(e, m);
    if (!r.ok) {
        std::printf("求值失败: %s\n", r.error.c_str());
        return 1;
    }
    if (r.value.im == 0.0)
        std::printf("%.12g\n", r.value.re);
    else
        std::printf("%.12g %+.12gi\n", r.value.re, r.value.im);
    return 0;
}

int CmdParse(const std::string& expr) {
    ExprP e = MustParse(expr);
    if (!e) return 1;
    std::printf("AST: %s\n", e->toStr().c_str());
    std::printf("节点数 %d  函数调用 %d  深度 %d\n", nodeCount(e), funcCallCount(e), maxDepth(e));
    std::printf("变量: ");
    for (auto& v : collectVars(e)) std::printf("%s ", v.c_str());
    std::printf("\n规范化: %s\n", canonicalKey(e).c_str());
    std::printf("化简: %s\n", simplify(e)->toStr().c_str());
    return 0;
}

int CmdDomain(const std::string& expr) {
    ExprP e = MustParse(expr);
    if (!e) return 1;
    DomainSet d = domainOf(e, "x");
    std::printf("定义域: %s\n", d.str().c_str());
    std::string why;
    bool an = isAnalyticOn(e, Interval::closed(0, 1), "x", &why);
    std::printf("在 [0,1] 解析: %s %s\n", an ? "是" : "否", why.c_str());
    return 0;
}

int CmdDiff(const std::string& expr) {
    ExprP e = MustParse(expr);
    if (!e) return 1;
    std::printf("f(x)  = %s\n", e->toStr().c_str());
    std::printf("f'(x) = %s\n", diff(e, "x")->toStr().c_str());
    std::printf("f''(x)= %s\n", diffN(e, "x", 2)->toStr().c_str());
    return 0;
}

int CmdLimit(const std::string& expr, double x0) {
    ExprP e = MustParse(expr);
    if (!e) return 1;
    auto r = limitAt(e, "x", x0);
    std::printf("x→%g: %s\n", x0, r.str().c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }
    std::string cmd = argv[1];
    try {
        if (cmd == "levels" && argc >= 3) return CmdLevels(argv[2]);
        if (cmd == "verify" && argc >= 3) return CmdVerify(argv[2]);
        if (cmd == "check" && argc >= 5) return CmdCheck(argv[2], argv[3], argv[4]);
        if (cmd == "endless" && argc >= 4)
            return CmdEndless(std::strtoull(argv[2], nullptr, 10), std::atoi(argv[3]));
        if (cmd == "eval" && argc >= 3) return CmdEval(argv[2], argc >= 4, argc >= 4 ? atof(argv[3]) : 0.0);
        if (cmd == "parse" && argc >= 3) return CmdParse(argv[2]);
        if (cmd == "domain" && argc >= 3) return CmdDomain(argv[2]);
        if (cmd == "diff" && argc >= 3) return CmdDiff(argv[2]);
        if (cmd == "limit" && argc >= 4) return CmdLimit(argv[2], atof(argv[3]));
    } catch (const std::exception& e) {
        std::printf("异常: %s\n", e.what());
        return 1;
    }
    PrintUsage();
    return 1;
}
