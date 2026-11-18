// Numeric.cpp — 复数算术与表达式求值
#include "Numeric.h"

#include <cmath>

namespace fcg {

Cplx operator+(Cplx a, Cplx b) { return Cplx(a.re + b.re, a.im + b.im); }
Cplx operator-(Cplx a, Cplx b) { return Cplx(a.re - b.re, a.im - b.im); }
Cplx operator-(Cplx a) { return Cplx(-a.re, -a.im); }

Cplx operator*(Cplx a, Cplx b) {
    return Cplx(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}

Cplx operator/(Cplx a, Cplx b) {
    double d = b.re * b.re + b.im * b.im;
    if (d == 0.0) return Cplx(std::nan(""), std::nan(""));
    return Cplx((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}

namespace {
constexpr double kEps = 1e-12;
bool nearlyZero(double v) { return std::fabs(v) < kEps; }
}  // namespace

Cplx cexp(Cplx z) {
    double m = std::exp(z.re);
    return Cplx(m * std::cos(z.im), m * std::sin(z.im));
}

Cplx clog(Cplx z) {
    if (z.re == 0.0 && z.im == 0.0) return Cplx(-INFINITY, 0.0);
    return Cplx(std::log(z.abs()), z.arg());
}

Cplx csqrt(Cplx z) {
    if (z.im == 0.0) {  // 含 -0.0：按实轴主值处理
        if (z.re >= 0.0) return Cplx(std::sqrt(z.re), 0.0);
        return Cplx(0.0, std::sqrt(-z.re));
    }
    double m = std::sqrt(z.abs());
    double t = z.arg() / 2.0;
    return Cplx(m * std::cos(t), m * std::sin(t));
}

Cplx cpow(Cplx a, Cplx b) {
    // 实底 + 整数指数：用连乘，避免分支切割问题
    if (nearlyZero(a.im) && nearlyZero(b.im)) {
        double e = b.re;
        if (std::fabs(e - std::round(e)) < 1e-9 && std::fabs(e) < 1000) {
            int n = static_cast<int>(std::round(e));
            double base = a.re;
            bool neg = n < 0;
            int m = neg ? -n : n;
            double r = 1.0;
            for (int k = 0; k < m; ++k) r *= base;
            if (neg) r = 1.0 / r;
            return Cplx(r, 0.0);
        }
    }
    if (a.re == 0.0 && a.im == 0.0) return Cplx(0.0, 0.0);
    return cexp(b * clog(a));
}

Cplx csin(Cplx z) {
    return Cplx(std::sin(z.re) * std::cosh(z.im), std::cos(z.re) * std::sinh(z.im));
}
Cplx ccos(Cplx z) {
    return Cplx(std::cos(z.re) * std::cosh(z.im), -std::sin(z.re) * std::sinh(z.im));
}
Cplx ctan(Cplx z) { return csin(z) / ccos(z); }

Cplx csinh(Cplx z) {
    return Cplx(std::sinh(z.re) * std::cos(z.im), std::cosh(z.re) * std::sin(z.im));
}
Cplx ccosh(Cplx z) {
    return Cplx(std::cosh(z.re) * std::cos(z.im), std::sinh(z.re) * std::sin(z.im));
}
Cplx ctanh(Cplx z) { return csinh(z) / ccosh(z); }

Cplx casin(Cplx z) {
    // asin(z) = -i ln(iz + sqrt(1 - z^2))
    Cplx i(0, 1);
    Cplx inner = i * z + csqrt(Cplx(1, 0) - z * z);
    Cplx l = clog(inner);
    return Cplx(l.im, -l.re);
}

Cplx cacos(Cplx z) {
    return Cplx(M_PI / 2 - casin(z).re, -casin(z).im);
}

Cplx catan(Cplx z) {
    // atan(z) = (i/2) ln((i+z)/(i-z))
    Cplx i(0, 1);
    Cplx q = (i + z) / (i - z);
    Cplx l = clog(q);
    return Cplx(-l.im / 2.0, l.re / 2.0);
}

namespace {

struct Ctx {
    const VarMap* vars;
    std::string error;
    bool hasError = false;
};

bool fail(Ctx& c, const std::string& msg) {
    if (!c.hasError) {
        c.error = msg;
        c.hasError = true;
    }
    return false;
}

bool evalRec(const ExprP& e, Ctx& c, Cplx& out);

bool evalRec(const ExprP& e, Ctx& c, Cplx& out) {
    if (!e) return fail(c, "空表达式");
    switch (e->op) {
        case Op::Num:
            out = Cplx(e->num);
            return true;
        case Op::Pi:
            out = Cplx(M_PI);
            return true;
        case Op::E:
            out = Cplx(M_E);
            return true;
        case Op::I:
            out = Cplx(0, 1);
            return true;
        case Op::Var: {
            auto it = c.vars->find(e->var);
            if (it == c.vars->end()) return fail(c, "未绑定的变量 " + e->var);
            out = it->second;
            return true;
        }
        case Op::Neg: {
            Cplx a;
            if (!evalRec(e->a, c, a)) return false;
            out = -a;
            return true;
        }
        case Op::Add:
        case Op::Sub:
        case Op::Mul:
        case Op::Div: {
            Cplx a, b;
            if (!evalRec(e->a, c, a)) return false;
            if (!evalRec(e->b, c, b)) return false;
            switch (e->op) {
                case Op::Add: out = a + b; break;
                case Op::Sub: out = a - b; break;
                case Op::Mul: out = a * b; break;
                default:
                    if (b.re == 0.0 && b.im == 0.0) return fail(c, "除数为 0");
                    out = a / b;
                    break;
            }
            return true;
        }
        case Op::Pow: {
            Cplx a, b;
            if (!evalRec(e->a, c, a)) return false;
            if (!evalRec(e->b, c, b)) return false;
            if (a.re == 0.0 && a.im == 0.0) {
                if (b.re > 0.0 && b.im == 0.0) {
                    out = Cplx(0, 0);
                    return true;
                }
                if (b.re == 0.0 && b.im == 0.0) {
                    out = Cplx(1, 0);
                    return true;
                }
                return fail(c, "0 的负数次幂无定义");
            }
            out = cpow(a, b);
            return true;
        }
        case Op::Mod: {
            Cplx a, b;
            if (!evalRec(e->a, c, a)) return false;
            if (!evalRec(e->b, c, b)) return false;
            if (a.im != 0.0 || b.im != 0.0) return fail(c, "mod 只支持实数");
            if (b.re == 0.0) return fail(c, "mod 除数为 0");
            out = Cplx(std::fmod(a.re, b.re));
            return true;
        }
        case Op::Min:
        case Op::Max: {
            Cplx a, b;
            if (!evalRec(e->a, c, a)) return false;
            if (!evalRec(e->b, c, b)) return false;
            if (a.im != 0.0 || b.im != 0.0) return fail(c, "min/max 只支持实数");
            out = Cplx(e->op == Op::Min ? std::min(a.re, b.re) : std::max(a.re, b.re));
            return true;
        }
        default: {
            Cplx a;
            if (!evalRec(e->a, c, a)) return false;
            switch (e->op) {
                case Op::Sin: out = csin(a); break;
                case Op::Cos: out = ccos(a); break;
                case Op::Tan: {
                    Cplx d = ccos(a);
                    if (d.abs() < 1e-15) return fail(c, "tan 在该点无定义");
                    out = csin(a) / d;
                    break;
                }
                case Op::Exp: out = cexp(a); break;
                case Op::Ln:
                    if (a.re == 0.0 && a.im == 0.0) return fail(c, "ln(0) 无定义");
                    out = clog(a);
                    break;
                case Op::Log2: {
                    if (a.re == 0.0 && a.im == 0.0) return fail(c, "log2(0) 无定义");
                    out = clog(a) / Cplx(std::log(2.0));
                    break;
                }
                case Op::Log10: {
                    if (a.re == 0.0 && a.im == 0.0) return fail(c, "log10(0) 无定义");
                    out = clog(a) / Cplx(std::log(10.0));
                    break;
                }
                case Op::Sqrt:
                    out = csqrt(a);
                    break;
                case Op::Cbrt:
                    if (a.im == 0.0) {
                        out = Cplx(std::cbrt(a.re));
                    } else {
                        out = cpow(a, Cplx(1.0 / 3.0));
                    }
                    break;
                case Op::Abs:
                    out = Cplx(a.abs());
                    break;
                case Op::Asin:
                    out = casin(a);
                    break;
                case Op::Acos:
                    out = cacos(a);
                    break;
                case Op::Atan: out = catan(a); break;
                case Op::Sinh: out = csinh(a); break;
                case Op::Cosh: out = ccosh(a); break;
                case Op::Tanh: out = ctanh(a); break;
                case Op::Asinh: {
                    // asinh(z) = ln(z + sqrt(z^2+1))
                    out = clog(a + csqrt(a * a + Cplx(1.0)));
                    break;
                }
                case Op::Acosh: {
                    out = clog(a + csqrt(a * a - Cplx(1.0)));
                    break;
                }
                case Op::Atanh: {
                    if (a.im == 0.0 && (a.re == 1.0 || a.re == -1.0))
                        return fail(c, "atanh(±1) 无定义");
                    out = Cplx(0.5) * clog((Cplx(1.0) + a) / (Cplx(1.0) - a));
                    break;
                }
                case Op::Floor:
                    if (a.im != 0.0) return fail(c, "floor 只支持实数");
                    out = Cplx(std::floor(a.re));
                    break;
                case Op::Ceil:
                    if (a.im != 0.0) return fail(c, "ceil 只支持实数");
                    out = Cplx(std::ceil(a.re));
                    break;
                case Op::Sign:
                    if (a.im != 0.0) return fail(c, "sign 只支持实数");
                    out = Cplx(a.re > 0 ? 1.0 : (a.re < 0 ? -1.0 : 0.0));
                    break;
                default:
                    return fail(c, std::string("未实现的运算 ") + opName(e->op));
            }
            if (std::isnan(out.re) || std::isnan(out.im)) return fail(c, "计算结果为 NaN");
            return true;
        }
    }
}

}  // namespace

EvalResult evaluate(const ExprP& e, const VarMap& vars) {
    EvalResult r;
    Ctx c;
    c.vars = &vars;
    Cplx out;
    if (!evalRec(e, c, out)) {
        r.ok = false;
        r.error = c.error;
        return r;
    }
    if (!out.isFinite()) {
        r.ok = false;
        r.error = "结果不是有限值";
        return r;
    }
    r.ok = true;
    r.value = out;
    return r;
}

EvalResult evaluateAt(const ExprP& e, const std::string& var, double x) {
    VarMap m;
    m[var] = Cplx(x);
    return evaluate(e, m);
}

RealResult evalReal(const ExprP& e, const std::string& var, double x) {
    RealResult r;
    auto e2 = evaluateAt(e, var, x);
    if (!e2.ok) {
        r.ok = false;
        r.error = e2.error;
        return r;
    }
    if (std::fabs(e2.value.im) > 1e-9) {
        r.ok = false;
        r.error = "结果为复数";
        return r;
    }
    r.ok = true;
    r.value = e2.value.re;
    return r;
}

}  // namespace fcg
