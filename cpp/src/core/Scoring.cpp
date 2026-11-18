// Scoring.cpp — 多维度评分
//
// 评分模型（权重之和为 1，总分上限 100）：
//   total = 100 * (0.35*precision + 0.30*elegance + 0.25*robustness + 0.10*speed)
// 硬约束（required=true）全部通过才算 correct；否则 total = 0。
#include "Scoring.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Numeric.h"
#include "Parser.h"

namespace fcg {
namespace {

constexpr double kWeightPrecision = 0.35;
constexpr double kWeightElegance = 0.30;
constexpr double kWeightRobustness = 0.25;
constexpr double kWeightSpeed = 0.10;

double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

bool finiteAt(const ExprP& f, const std::string& var, double x, double* out) {
    if (!f) return false;
    const RealResult r = evalReal(f, var, x);
    if (!r.ok || !std::isfinite(r.value)) return false;
    if (out) *out = r.value;
    return true;
}

// 复杂度度量：函数调用代价高，节点数次之
double complexityOf(int funcCalls, int nodes) {
    return static_cast<double>(funcCalls) + 0.35 * static_cast<double>(nodes);
}

bool finiteInterval(const Interval& iv, double* a, double* b) {
    if (iv.loInf || iv.hiInf) return false;
    if (!(iv.hi > iv.lo)) return false;
    *a = iv.lo;
    *b = iv.hi;
    return true;
}

// -----------------------------------------------------------------
// 精确度：边界值 / 值域类约束的数值误差（误差 0 → 100）
// -----------------------------------------------------------------
double computePrecision(const ExprP& f, const LevelDef& level) {
    double sum = 0.0;
    int n = 0;
    for (const Constraint& c : level.constraints) {
        if (!c.required) continue;
        if (c.kind == ConstraintKind::BoundaryValue) {
            double v = 0.0;
            double relErr = 0.0;
            if (finiteAt(f, c.var, c.point, &v)) {
                const double scale = std::max(1.0, std::fabs(c.value));
                relErr = std::fabs(v - c.value) / scale;
            } else {
                relErr = 1.0;
            }
            // 相对误差 1e-3 → 约 37 分；误差 0 → 100 分
            sum += 100.0 * std::exp(-relErr / 1e-3);
            ++n;
        } else if (c.kind == ConstraintKind::RangeOn) {
            // 用采样估计值域，超出上/下界的程度折算成误差
            double a = -10.0, b = 10.0;
            const bool finite = finiteInterval(c.interval, &a, &b);
            if (!finite) {
                a = -10.0;
                b = 10.0;
            }
            double lo = 0.0, hi = 0.0, maxAbs = 0.0;
            int okCnt = 0;
            for (int i = 0; i <= 256; ++i) {
                const double x = a + (b - a) * static_cast<double>(i) / 256.0;
                double v = 0.0;
                if (!finiteAt(f, c.var, x, &v)) continue;
                if (okCnt == 0) {
                    lo = hi = v;
                } else {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                maxAbs = std::max(maxAbs, std::fabs(v));
                ++okCnt;
            }
            double err = 0.0;
            if (okCnt < 8) {
                err = 1.0;
            } else {
                const double scale = std::max(1.0, std::fabs(c.lo) + std::fabs(c.hi));
                const double overLo = std::max(0.0, c.lo - lo) / scale;
                const double overHi = std::max(0.0, hi - c.hi) / scale;
                err = std::max(overLo, overHi);
            }
            sum += 100.0 * std::exp(-err / 1e-2);
            ++n;
        }
    }
    if (n == 0) return 100.0;  // 没有数值型约束：视为精确
    return sum / static_cast<double>(n);
}

// -----------------------------------------------------------------
// 优雅度：与参考解的 AST 复杂度比较，越简洁越高
// -----------------------------------------------------------------
double computeElegance(const ExprP& f, const LevelDef& level, int funcCalls, int nodes) {
    int refFuncs = 3;
    int refNodes = 9;
    if (!level.reference.empty()) {
        ParseError pe;
        ExprP ref = parseExpr(level.reference, &pe);
        if (ref) {
            refFuncs = funcCallCount(ref);
            refNodes = nodeCount(ref);
        }
    }
    // 下限保护：函数调用很少时直接给满分
    if (funcCalls <= 2) return 100.0;
    const double mine = complexityOf(funcCalls, nodes);
    const double ref = complexityOf(refFuncs, refNodes);
    if (ref <= 0.0) return 100.0;
    if (mine <= ref) return 100.0;
    const double score = 100.0 * ref / mine;
    return std::max(0.0, std::min(100.0, score));
}

// -----------------------------------------------------------------
// 鲁棒性：约束区间边界附近（端点内缩 / 外扩 1e-3）的表现
//   * 内缩点：不仅要求可求值，还要求该约束在边界内侧仍然成立
//     （恒正 / 值域 / 内点可导）；
//   * 外扩点：只要求函数仍然可求值（约束本就不要求区间外的行为）。
// -----------------------------------------------------------------
struct ProbeAccumulator {
    int total = 0;
    int pass = 0;
    void add() {
        ++total;
    }
    double score() const {
        if (total == 0) return -1.0;  // 没有探针
        return 100.0 * static_cast<double>(pass) / static_cast<double>(total);
    }
};

double computeRobustness(const ExprP& f, const LevelDef& level, bool correct, int hardTotal,
                         int hardPass) {
    ProbeAccumulator acc;
    // 在点 x 处采样一次；enforce 时额外要求该约束性质在 x 处（仍）成立
    auto probe = [&](const Constraint& c, double x, bool enforce) {
        acc.add();
        double v = 0.0;
        if (!finiteAt(f, c.var, x, &v)) return;
        if (enforce) {
            switch (c.kind) {
                case ConstraintKind::PositiveOn:
                    if (!(v > 0.0)) return;
                    break;
                case ConstraintKind::RangeOn: {
                    const double tol = 1e-6 * (1.0 + std::fabs(c.lo) + std::fabs(c.hi));
                    if (v < c.lo - tol || v > c.hi + tol) return;
                    break;
                }
                case ConstraintKind::AnalyticOn: {
                    // 数值上要求该点附近左右导数存在且有限
                    const double h = 1e-6 * std::max(1.0, std::fabs(x));
                    double left = 0.0, right = 0.0;
                    if (!finiteAt(f, c.var, x - h, &left) ||
                        !finiteAt(f, c.var, x + h, &right))
                        return;
                    if (!std::isfinite((right - left) / (2.0 * h))) return;
                    break;
                }
                default:
                    break;
            }
        }
        ++acc.pass;
    };

    for (const Constraint& c : level.constraints) {
        if (!c.required) continue;
        double a = 0.0, b = 0.0;
        const bool finite = finiteInterval(c.interval, &a, &b);
        const double eps = finite ? 1e-3 * std::max(1.0, b - a) : 1e-3;
        switch (c.kind) {
            case ConstraintKind::DomainOn:
            case ConstraintKind::AnalyticOn:
            case ConstraintKind::Monotonic:
            case ConstraintKind::RangeOn:
            case ConstraintKind::PositiveOn:
            case ConstraintKind::ZeroCount: {
                if (!finite) break;
                // 端点内缩：靠近边界仍满足
                probe(c, a + eps, true);
                probe(c, b - eps, true);
                // 端点外扩：边界之外函数仍然可求值
                probe(c, a - eps, false);
                probe(c, b + eps, false);
                // 区间内部抽查，避免区间内有奇点却得分虚高
                probe(c, a + (b - a) * 0.25, true);
                probe(c, a + (b - a) * 0.5, true);
                probe(c, a + (b - a) * 0.75, true);
                break;
            }
            case ConstraintKind::BoundaryValue:
            case ConstraintKind::Extremum:
            case ConstraintKind::SymmetryAxis:
            case ConstraintKind::PeriodicSymmetry: {
                probe(c, c.point - eps, false);
                probe(c, c.point, false);
                probe(c, c.point + eps, false);
                break;
            }
            case ConstraintKind::Asymptote:
            case ConstraintKind::Parity:
            case ConstraintKind::Periodic:
            case ConstraintKind::AllowedFunctions:
            case ConstraintKind::ComplexityLimit:
                break;
        }
    }
    const double probeScore = acc.score();
    if (probeScore >= 0.0) return probeScore;
    // 没有可用的点探针：退化为"硬约束通过比例"
    if (hardTotal == 0) return correct ? 100.0 : 0.0;
    return 100.0 * static_cast<double>(hardPass) / static_cast<double>(hardTotal);
}

}  // namespace

char gradeOf(double total) {
    if (total >= 90.0) return 'S';
    if (total >= 80.0) return 'A';
    if (total >= 70.0) return 'B';
    if (total >= 60.0) return 'C';
    return 'D';
}

const char* gradeLabel(char grade) {
    switch (grade) {
        case 'S': return "S · 完美解";
        case 'A': return "A · 优秀解";
        case 'B': return "B · 良好解";
        case 'C': return "C · 及格";
        default: return "D · 未通过";
    }
}

bool checkAllConstraints(const ExprP& f, const LevelDef& level,
                         std::vector<ConstraintReport>* reports) {
    bool allOk = true;
    if (reports) reports->clear();
    for (const Constraint& c : level.constraints) {
        ConstraintReport r = checkConstraint(c, f);
        if (r.label.empty()) r.label = c.describe();
        if (c.required && !r.ok) allOk = false;
        if (reports) reports->push_back(r);
    }
    return allOk;
}

ScoreBreakdown evaluateSubmission(const ExprP& f, const LevelDef& level, double elapsedSec) {
    ScoreBreakdown sb;
    sb.elapsed = elapsedSec;

    if (!f) {
        sb.correct = false;
        sb.total = 0.0;
        sb.grade = 'D';
        sb.gradeLabel = gradeLabel('D');
        sb.feedback.push_back("表达式为空或无法解析，先写一个函数再提交吧。");
        for (const Constraint& c : level.constraints) {
            ConstraintReport r;
            r.kind = c.kind;
            r.label = c.describe();
            r.ok = false;
            r.detail = "表达式为空，无法验证";
            sb.reports.push_back(r);
        }
        return sb;
    }

    sb.nodeCnt = nodeCount(f);
    sb.funcCalls = funcCallCount(f);

    int hardTotal = 0;
    int hardPass = 0;
    bool allOk = true;
    for (const Constraint& c : level.constraints) {
        ConstraintReport r = checkConstraint(c, f);
        if (r.label.empty()) r.label = c.describe();
        if (c.required) {
            ++hardTotal;
            if (r.ok) {
                ++hardPass;
            } else {
                allOk = false;
            }
        }
        sb.reports.push_back(r);
    }
    sb.correct = allOk;

    sb.precision = computePrecision(f, level);
    sb.elegance = computeElegance(f, level, sb.funcCalls, sb.nodeCnt);
    sb.robustness = computeRobustness(f, level, sb.correct, hardTotal, hardPass);

    if (level.timeLimit > 0.0) {
        const double remain = (level.timeLimit - elapsedSec) / level.timeLimit;
        sb.speed = 100.0 * clamp01(remain);
    } else {
        sb.speed = 100.0;
    }

    if (sb.correct) {
        sb.total = 100.0 * (kWeightPrecision * sb.precision / 100.0 +
                            kWeightElegance * sb.elegance / 100.0 +
                            kWeightRobustness * sb.robustness / 100.0 +
                            kWeightSpeed * sb.speed / 100.0);
        sb.total = std::max(0.0, std::min(100.0, sb.total));
    } else {
        sb.total = 0.0;
    }
    sb.grade = sb.correct ? gradeOf(sb.total) : 'D';
    sb.gradeLabel = gradeLabel(sb.grade);

    // ---------------- 中文反馈 ----------------
    if (!sb.correct) {
        int shown = 0;
        for (const ConstraintReport& r : sb.reports) {
            if (r.ok || r.detail.empty()) continue;
            if (shown >= 3) break;
            sb.feedback.push_back("「" + r.label + "」未通过：" + r.detail);
            ++shown;
        }
        if (sb.feedback.empty()) sb.feedback.push_back("还没有满足全部硬约束，对照约束面板再试一次。");
    } else {
        if (sb.elegance < 70.0) {
            sb.feedback.push_back("表达式可以更简洁：试着减少函数调用或嵌套层次。");
        }
        if (sb.funcCalls > 2 && sb.elegance >= 70.0) {
            sb.feedback.push_back("已经比较简洁了，把函数调用数再压一压可以冲击 S 级。");
        }
        if (sb.precision < 99.0) {
            sb.feedback.push_back("边界值还差一点点：检查 f(a)、f(b) 处的取值是否精确。");
        }
        if (sb.robustness < 90.0) {
            sb.feedback.push_back("在约束边界附近函数表现不够稳定，试试换成全局有定义的函数。");
        }
        if (level.timeLimit > 0.0 && sb.speed < 60.0) {
            sb.feedback.push_back("提交得再快一点，时间加成还能提高。");
        }
        if (!level.reference.empty()) {
            ParseError pe;
            ExprP ref = parseExpr(level.reference, &pe);
            if (ref) {
                sb.feedback.push_back("参考解用了 " + std::to_string(funcCallCount(ref)) +
                                      " 个函数调用、" + std::to_string(nodeCount(ref)) +
                                      " 个节点。");
            }
        }
        if (sb.grade == 'S') sb.feedback.push_back("完美的解！");
        else if (sb.grade == 'A') sb.feedback.push_back("很棒，离 S 只差一点点了。");
        else if (sb.grade == 'B') sb.feedback.push_back("通过了，但还可以更优雅。");
        else sb.feedback.push_back("虽然通过了，但还可以优化精度或复杂度。");
    }
    return sb;
}

}  // namespace fcg
