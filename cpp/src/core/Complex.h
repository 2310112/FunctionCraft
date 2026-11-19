// Complex.h — 复数域与复变函数分析
//
// 设计说明：
//   · 复数变量统一用变量名（默认 "z"），表达式树无需变化：z 只是一个 Var
//   · 复数值类型复用 Numeric.h 的 Cplx
//   · 全纯性（解析性）判定 = 结构检查（无非全纯算子）+ 柯西-黎曼数值验证 + 区域内无奇点
//   · 留数/积分/零点数用数值围道积分（对小半径圆积分，比高阶数值求导稳定得多）
#pragma once

#include <string>
#include <vector>

#include "Expr.h"
#include "Numeric.h"

namespace fcg {

// ---------------------------------------------------------------
// 复平面区域
// ---------------------------------------------------------------
struct ComplexRegion {
    enum class Kind {
        Disk,            // |z - center| < radius
        Annulus,         // radius < |z - center| < outerRadius
        Rectangle,       // x0 < Re z < x1 且 y0 < Im z < y1
        HalfPlaneUpper,  // Im z > yLine
        HalfPlaneLower,  // Im z < yLine
        HalfPlaneRight,  // Re z > xLine
        ExteriorOfDisk,  // |z - center| > radius
        Plane,           // 整个复平面
    };

    Kind kind = Kind::Disk;
    Cplx center{0.0, 0.0};
    double radius = 1.0;       // Disk / ExteriorOfDisk 半径；Annulus 内半径
    double outerRadius = 2.0;  // Annulus 外半径
    double x0 = -1.0, x1 = 1.0, y0 = -1.0, y1 = 1.0;  // Rectangle
    double yLine = 0.0;        // 半平面分界
    double xLine = 0.0;        // 半平面分界

    bool contains(Cplx z, double tol = 1e-12) const;
    bool isEmpty() const;
    std::string str() const;  // 中文/符号描述，如 "|z| < 1"、"1 < |z| < 2"、"上半平面"

    static ComplexRegion disk(Cplx c, double r);
    static ComplexRegion annulus(Cplx c, double rIn, double rOut);
    static ComplexRegion rectangle(double ax, double bx, double ay, double by);
    static ComplexRegion wholePlane();
};

// ---------------------------------------------------------------
// 通用结果包装
// ---------------------------------------------------------------
struct CplxResult {
    bool ok = false;
    Cplx value;
    std::string error;
};

struct ComplexCheck {
    bool ok = false;
    std::string reason;  // 失败原因（中文）
};

struct IntResult {
    bool ok = false;
    int value = 0;
    std::string error;
};

struct ModRangeResult {
    bool ok = false;
    double lo = 0.0, hi = 0.0;
    std::string error;
};

// ---------------------------------------------------------------
// 全纯性
// ---------------------------------------------------------------
// 该算子是否"全纯"（在定义域内解析）：Re/Im/Conj/Arg/Abs/min/max/floor/... 都不是
bool opIsHolomorphic(Op op);

// 表达式中是否有非全纯算子（*badOp 返回第一个）
bool builtFromHolomorphicOps(const ExprP& f, Op* badOp = nullptr);

// 柯西-黎曼验证：在 region 内采样点检查 ∂f/∂x = (1/i)·∂f/∂y
// 返回不满足的最大残差（相对）；若无采样点则为 0
double cauchyRiemannResidual(const ExprP& f, const std::string& var, const ComplexRegion& region,
                             int samples = 64);

// 综合判定：在 region 上全纯（解析）
ComplexCheck isHolomorphicOn(const ExprP& f, const std::string& var, const ComplexRegion& region);

// ---------------------------------------------------------------
// 奇点 / 极点 / 留数
// ---------------------------------------------------------------
struct SingularPoint {
    Cplx z;
    int poleOrder = 0;    // >0 极点阶数；0 表示可去奇点；-1 表示本质奇点
    bool essential = false;
};

// 区域内的孤立奇点（数值搜索）
std::vector<SingularPoint> singularitiesIn(const ExprP& f, const std::string& var,
                                           const ComplexRegion& region);

// 点 z0 处的留数：对以 z0 为心的小圆做围道积分
CplxResult residueAt(const ExprP& f, const std::string& var, Cplx z0);

// 圆周围道积分 ∮_{|z-c|=r} f(z) dz（逆时针）
CplxResult contourIntegral(const ExprP& f, const std::string& var, Cplx center, double radius);

// 区域内零点个数（辐角原理：N = (1/2πi)∮ f'/f dz，减极点）
IntResult zeroCountIn(const ExprP& f, const std::string& var, const ComplexRegion& region);

// ---------------------------------------------------------------
// 其他
// ---------------------------------------------------------------
// 区域内 |f(z)| 的取值范围（数值扫描）
ModRangeResult modulusRangeOn(const ExprP& f, const std::string& var, const ComplexRegion& region);

// 表达式是否涉及复数（含 i、含复算子、或对复变量求值后非实数）
bool usesComplexOps(const ExprP& f);

// 单点求值（复数），带错误信息
CplxResult evalComplex(const ExprP& f, const std::string& var, Cplx z);

}  // namespace fcg
