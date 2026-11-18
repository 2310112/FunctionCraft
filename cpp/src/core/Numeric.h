// Numeric.h — 数值类型与求值（实数/复数统一表示）
#pragma once

#include <cmath>
#include <map>
#include <string>

#include "Expr.h"

namespace fcg {

// 复数（real 模式下 im 恒为 0）
struct Cplx {
    double re = 0.0;
    double im = 0.0;

    Cplx() = default;
    Cplx(double r, double i = 0.0) : re(r), im(i) {}

    bool isReal() const { return im == 0.0; }
    bool isFinite() const { return std::isfinite(re) && std::isfinite(im); }
    bool isNaN() const { return std::isnan(re) || std::isnan(im); }
    double abs() const { return std::hypot(re, im); }
    double arg() const { return std::atan2(im, re); }
    Cplx conj() const { return Cplx(re, -im); }
};

Cplx operator+(Cplx a, Cplx b);
Cplx operator-(Cplx a, Cplx b);
Cplx operator-(Cplx a);
Cplx operator*(Cplx a, Cplx b);
Cplx operator/(Cplx a, Cplx b);

Cplx cexp(Cplx z);
Cplx clog(Cplx z);
Cplx cpow(Cplx a, Cplx b);  // 主值
Cplx csqrt(Cplx z);
Cplx csin(Cplx z);
Cplx ccos(Cplx z);
Cplx ctan(Cplx z);
Cplx casin(Cplx z);
Cplx cacos(Cplx z);
Cplx catan(Cplx z);
Cplx csinh(Cplx z);
Cplx ccosh(Cplx z);
Cplx ctanh(Cplx z);

using VarMap = std::map<std::string, Cplx>;

// 求值结果
struct EvalResult {
    bool ok = false;
    Cplx value;
    std::string error;  // 失败原因（"log 的参数 <= 0" 等）
};

EvalResult evaluate(const ExprP& e, const VarMap& vars);
EvalResult evaluateAt(const ExprP& e, const std::string& var, double x);

// 实数便捷包装：非实数结果返回 ok=false
struct RealResult {
    bool ok = false;
    double value = 0.0;
    std::string error;
};
RealResult evalReal(const ExprP& e, const std::string& var, double x);

}  // namespace fcg
