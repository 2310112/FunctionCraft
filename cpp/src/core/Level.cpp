// Level.cpp — 关卡数据模型与 JSON 加载
#include "Level.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "Json.h"
#include "Numeric.h"
#include "Parser.h"

namespace fcg {
namespace {

// -----------------------------------------------------------------
// 基础读取工具
// -----------------------------------------------------------------
bool jsonToNumber(const Json& v, double* out, std::string* err, const std::string& what) {
    if (v.isNumber()) {
        *out = v.asNumber();
        return true;
    }
    if (v.isString()) {
        ParseError pe;
        ExprP e = parseExpr(v.asString(), &pe);
        if (!e) {
            if (err) *err = what + " 的表达式无法解析：" + pe.message;
            return false;
        }
        if (hasVar(e)) {
            if (err) *err = what + " 必须是常数表达式（不能含变量）";
            return false;
        }
        VarMap vm;
        const EvalResult r = evaluate(e, vm);
        if (!r.ok) {
            if (err) *err = what + " 求值失败：" + r.error;
            return false;
        }
        if (std::fabs(r.value.im) > 1e-12 || !std::isfinite(r.value.re)) {
            if (err) *err = what + " 必须是有限实数";
            return false;
        }
        *out = r.value.re;
        return true;
    }
    if (err) *err = what + " 必须是数字或常数表达式字符串";
    return false;
}

bool readNumField(const Json& obj, const std::string& key, double* out, std::string* err,
                  const std::string& where, bool required, double def = 0.0) {
    if (!obj.has(key)) {
        if (required) {
            if (err) *err = where + " 缺少字段 \"" + key + "\"";
            return false;
        }
        *out = def;
        return true;
    }
    return jsonToNumber(obj[key], out, err, where + " 的字段 \"" + key + "\"");
}

// 从多个别名中读取数字字段（第一个存在的生效）
bool readNumFieldAny(const Json& obj, const std::vector<std::string>& keys, double* out,
                     std::string* err, const std::string& where, bool required,
                     double def = 0.0) {
    for (const std::string& k : keys) {
        if (obj.has(k)) return readNumField(obj, k, out, err, where, true);
    }
    if (required) {
        if (err) *err = where + " 缺少字段 \"" + keys.front() + "\"";
        return false;
    }
    *out = def;
    return true;
}

bool readIntFieldAny(const Json& obj, const std::vector<std::string>& keys, int* out,
                     std::string* err, const std::string& where, bool required,
                     int def = 0) {
    for (const std::string& k : keys) {
        if (!obj.has(k)) continue;
        const Json& v = obj[k];
        if (v.isNumber()) {
            *out = v.asInt(def);
            return true;
        }
        if (v.isString()) {
            double d = 0.0;
            if (!jsonToNumber(v, &d, err, where + " 的字段 \"" + k + "\"")) return false;
            *out = static_cast<int>(std::lround(d));
            return true;
        }
        if (err) *err = where + " 的字段 \"" + k + "\" 必须是整数";
        return false;
    }
    if (required) {
        if (err) *err = where + " 缺少字段 \"" + keys.front() + "\"";
        return false;
    }
    *out = def;
    return true;
}

// 区间：支持 [lo, hi] 数组或 {lo,hi,loClosed,hiClosed,open} 对象
bool parseIntervalJson(const Json& v, Interval* out, std::string* err,
                       const std::string& where) {
    if (v.isArray()) {
        if (v.size() != 2) {
            if (err) *err = where + " 的区间必须是 [下界, 上界]";
            return false;
        }
        double lo = 0.0, hi = 0.0;
        if (!jsonToNumber(v.at(0), &lo, err, where + " 的区间下界")) return false;
        if (!jsonToNumber(v.at(1), &hi, err, where + " 的区间上界")) return false;
        if (!(hi > lo)) {
            if (err) *err = where + " 的区间上界必须大于下界";
            return false;
        }
        *out = Interval::closed(lo, hi);
        return true;
    }
    if (v.isObject()) {
        Interval iv;  // 默认 [-∞, +∞]
        iv.lo = -1e308;
        iv.hi = 1e308;
        iv.loInf = true;
        iv.hiInf = true;
        iv.loClosed = true;
        iv.hiClosed = true;
        if (v.has("lo")) {
            if (!jsonToNumber(v["lo"], &iv.lo, err, where + " 的区间下界")) return false;
            iv.loInf = false;
        }
        if (v.has("hi")) {
            if (!jsonToNumber(v["hi"], &iv.hi, err, where + " 的区间上界")) return false;
            iv.hiInf = false;
        }
        if (v.has("loClosed")) iv.loClosed = v["loClosed"].asBool(true);
        if (v.has("hiClosed")) iv.hiClosed = v["hiClosed"].asBool(true);
        if (v.has("open") && v["open"].asBool(false)) {
            iv.loClosed = false;
            iv.hiClosed = false;
        }
        if (!iv.loInf && !iv.hiInf && !(iv.hi > iv.lo)) {
            if (err) *err = where + " 的区间上界必须大于下界";
            return false;
        }
        *out = iv;
        return true;
    }
    if (err) *err = where + " 的区间必须是 [下界, 上界] 数组或 {lo, hi} 对象";
    return false;
}

// -----------------------------------------------------------------
// 类型字符串 ↔ 约束种类
// -----------------------------------------------------------------
bool kindFromString(const std::string& s, ConstraintKind* out) {
    static const std::map<std::string, ConstraintKind> table = {
        {"allowed_functions", ConstraintKind::AllowedFunctions},
        {"allowed", ConstraintKind::AllowedFunctions},
        {"functions", ConstraintKind::AllowedFunctions},
        {"function_library", ConstraintKind::AllowedFunctions},
        {"domain", ConstraintKind::DomainOn},
        {"domain_on", ConstraintKind::DomainOn},
        {"defined", ConstraintKind::DomainOn},
        {"analytic", ConstraintKind::AnalyticOn},
        {"analytic_on", ConstraintKind::AnalyticOn},
        {"analyticity", ConstraintKind::AnalyticOn},
        {"boundary", ConstraintKind::BoundaryValue},
        {"boundary_value", ConstraintKind::BoundaryValue},
        {"value_at", ConstraintKind::BoundaryValue},
        {"range", ConstraintKind::RangeOn},
        {"range_on", ConstraintKind::RangeOn},
        {"positive", ConstraintKind::PositiveOn},
        {"positive_on", ConstraintKind::PositiveOn},
        {"monotonic", ConstraintKind::Monotonic},
        {"monotonicity", ConstraintKind::Monotonic},
        {"parity", ConstraintKind::Parity},
        {"even_odd", ConstraintKind::Parity},
        {"even", ConstraintKind::Parity},
        {"odd", ConstraintKind::Parity},
        {"periodic", ConstraintKind::Periodic},
        {"period", ConstraintKind::Periodic},
        {"asymptote", ConstraintKind::Asymptote},
        {"horizontal_asymptote", ConstraintKind::Asymptote},
        {"extremum", ConstraintKind::Extremum},
        {"extreme", ConstraintKind::Extremum},
        {"symmetry_axis", ConstraintKind::SymmetryAxis},
        {"symmetry", ConstraintKind::SymmetryAxis},
        {"zero_count", ConstraintKind::ZeroCount},
        {"zeros", ConstraintKind::ZeroCount},
        {"zero", ConstraintKind::ZeroCount},
        {"complexity_limit", ConstraintKind::ComplexityLimit},
        {"complexity", ConstraintKind::ComplexityLimit},
        {"point_symmetry", ConstraintKind::PeriodicSymmetry},
        {"periodic_symmetry", ConstraintKind::PeriodicSymmetry},
        {"center_symmetry", ConstraintKind::PeriodicSymmetry},
        // ---------- 复变 ----------
        {"holomorphic", ConstraintKind::HolomorphicOn},
        {"analytic_complex", ConstraintKind::HolomorphicOn},
        {"no_singularity", ConstraintKind::NoSingularityIn},
        {"singularity_free", ConstraintKind::NoSingularityIn},
        {"pole_order", ConstraintKind::PoleOrderAt},
        {"pole", ConstraintKind::PoleOrderAt},
        {"residue", ConstraintKind::ResidueAt},
        {"residues", ConstraintKind::ResidueAt},
        {"contour_integral", ConstraintKind::ContourIntegralValue},
        {"contour", ConstraintKind::ContourIntegralValue},
        {"integral", ConstraintKind::ContourIntegralValue},
        {"zero_count_region", ConstraintKind::ZeroCountInRegion},
        {"zeros_region", ConstraintKind::ZeroCountInRegion},
        {"modulus_range", ConstraintKind::ModulusRangeOn},
        {"modulus", ConstraintKind::ModulusRangeOn},
        {"value_complex", ConstraintKind::ComplexBoundaryValue},
        {"complex_value", ConstraintKind::ComplexBoundaryValue},
    };
    auto it = table.find(s);
    if (it == table.end()) return false;
    *out = it->second;
    return true;
}

// 函数库里的名字 → Op（与 Parser 的别名保持一致）
bool opFromName(const std::string& raw, Op* out) {
    std::string s = raw;
    // 去掉首尾空白
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();

    static const std::map<std::string, Op> table = {
        {"sin", Op::Sin},     {"cos", Op::Cos},     {"tan", Op::Tan},
        {"exp", Op::Exp},     {"ln", Op::Ln},       {"log", Op::Ln},
        {"log2", Op::Log2},   {"lg", Op::Log10},    {"log10", Op::Log10},
        {"sqrt", Op::Sqrt},   {"√", Op::Sqrt},      {"cbrt", Op::Cbrt},
        {"abs", Op::Abs},     {"|x|", Op::Abs},
        {"asin", Op::Asin},   {"arcsin", Op::Asin},
        {"acos", Op::Acos},   {"arccos", Op::Acos},
        {"atan", Op::Atan},   {"arctan", Op::Atan},
        {"sinh", Op::Sinh},   {"cosh", Op::Cosh},   {"tanh", Op::Tanh},
        {"asinh", Op::Asinh}, {"acosh", Op::Acosh}, {"atanh", Op::Atanh},
        {"floor", Op::Floor}, {"ceil", Op::Ceil},   {"sign", Op::Sign},
        {"sgn", Op::Sign},    {"min", Op::Min},     {"max", Op::Max},
        {"mod", Op::Mod},
        {"+", Op::Add},       {"add", Op::Add},
        {"-", Op::Sub},       {"sub", Op::Sub},
        {"*", Op::Mul},       {"×", Op::Mul},       {"·", Op::Mul}, {"mul", Op::Mul},
        {"/", Op::Div},       {"÷", Op::Div},       {"div", Op::Div},
        {"^", Op::Pow},       {"**", Op::Pow},      {"pow", Op::Pow},
        {"neg", Op::Neg},
    };
    auto it = table.find(s);
    if (it == table.end()) return false;
    *out = it->second;
    return true;
}

// -----------------------------------------------------------------
// 复数与复平面区域解析
// -----------------------------------------------------------------
// 复数三种写法：数字（实部）、字符串表达式（"2*pi*i"）、[实部, 虚部]
bool parseComplexValue(const Json& v, Cplx* out, std::string* err, const std::string& where) {
    if (v.isNumber()) {
        *out = Cplx(v.asNumber(), 0.0);
        return true;
    }
    if (v.isString()) {
        ParseError pe;
        ExprP e = parseExpr(v.asString(), &pe);
        if (!e) {
            if (err) *err = where + " 的复数表达式无法解析：" + pe.message;
            return false;
        }
        VarMap none;
        auto r = evaluate(e, none);
        if (!r.ok) {
            if (err) *err = where + " 的复数表达式无法求值：" + r.error;
            return false;
        }
        *out = r.value;
        return true;
    }
    if (v.isArray() && v.size() == 2) {
        double re = 0, im = 0;
        if (!jsonToNumber(v.at(0), &re, err, where + " 的实部")) return false;
        if (!jsonToNumber(v.at(1), &im, err, where + " 的虚部")) return false;
        *out = Cplx(re, im);
        return true;
    }
    if (err) *err = where + " 必须是数字、表达式字符串或 [实部, 虚部]";
    return false;
}

bool readNumOr(const Json& obj, const char* key, double def, double* out) {
    if (obj.has(key) && obj[key].isNumber()) {
        *out = obj[key].asNumber();
        return true;
    }
    *out = def;
    return false;
}

bool parseComplexRegion(const Json& v, ComplexRegion* out, std::string* err,
                        const std::string& where) {
    if (!v.isObject()) {
        if (err) *err = where + " 的 \"region\" 必须是对象";
        return false;
    }
    std::string kind = v.has("kind") && v["kind"].isString() ? v["kind"].asString() : "disk";
    ComplexRegion r;
    auto centerOf = [&](Cplx def) {
        if (v.has("center")) {
            Cplx c = def;
            if (parseComplexValue(v["center"], &c, err, where + " 的坐标中心")) return c;
        }
        return def;
    };
    if (kind == "disk" || kind == "circle") {
        r.kind = ComplexRegion::Kind::Disk;
        r.center = centerOf(Cplx(0, 0));
        r.radius = v.has("radius") ? v["radius"].asNumber(1.0) : 1.0;
    } else if (kind == "annulus" || kind == "ring") {
        r.kind = ComplexRegion::Kind::Annulus;
        r.center = centerOf(Cplx(0, 0));
        r.radius = v.has("radius") ? v["radius"].asNumber(1.0) : 1.0;
        r.outerRadius = v.has("outerRadius") ? v["outerRadius"].asNumber(2.0)
                                             : (v.has("outer_radius") ? v["outer_radius"].asNumber(2.0) : 2.0);
    } else if (kind == "rectangle" || kind == "rect") {
        r.kind = ComplexRegion::Kind::Rectangle;
        readNumOr(v, "x0", -1.0, &r.x0);
        readNumOr(v, "x1", 1.0, &r.x1);
        readNumOr(v, "y0", -1.0, &r.y0);
        readNumOr(v, "y1", 1.0, &r.y1);
    } else if (kind == "half_upper" || kind == "upper_half_plane") {
        r.kind = ComplexRegion::Kind::HalfPlaneUpper;
        readNumOr(v, "yLine", 0.0, &r.yLine);
    } else if (kind == "half_lower" || kind == "lower_half_plane") {
        r.kind = ComplexRegion::Kind::HalfPlaneLower;
        readNumOr(v, "yLine", 0.0, &r.yLine);
    } else if (kind == "half_right" || kind == "right_half_plane") {
        r.kind = ComplexRegion::Kind::HalfPlaneRight;
        readNumOr(v, "xLine", 0.0, &r.xLine);
    } else if (kind == "exterior_disk" || kind == "outside") {
        r.kind = ComplexRegion::Kind::ExteriorOfDisk;
        r.center = centerOf(Cplx(0, 0));
        r.radius = v.has("radius") ? v["radius"].asNumber(1.0) : 1.0;
    } else if (kind == "plane" || kind == "all") {
        r.kind = ComplexRegion::Kind::Plane;
    } else {
        if (err) *err = where + " 的 region.kind 未知：\"" + kind + "\"";
        return false;
    }
    *out = r;
    return true;
}

// -----------------------------------------------------------------
// 单条约束解析
// -----------------------------------------------------------------
bool constraintFromJson(const Json& j, Constraint* out, std::string* err,
                        const std::string& where) {
    if (!j.isObject()) {
        if (err) *err = where + " 必须是 JSON 对象";
        return false;
    }
    if (!j.has("type") || !j["type"].isString()) {
        if (err) *err = where + " 缺少字符串字段 \"type\"";
        return false;
    }
    const std::string type = j["type"].asString();
    ConstraintKind kind;
    if (!kindFromString(type, &kind)) {
        if (err) *err = where + " 未知的约束类型 \"" + type + "\"";
        return false;
    }

    Constraint c;
    c.kind = kind;
    if (j.has("var") && j["var"].isString() && !j["var"].asString().empty())
        c.var = j["var"].asString();
    if (j.has("label") && j["label"].isString()) c.label = j["label"].asString();
    if (j.has("required")) c.required = j["required"].asBool(true);

    bool hasInterval = false;
    if (j.has("interval")) {
        if (!parseIntervalJson(j["interval"], &c.interval, err, where)) return false;
        hasInterval = true;
    }

    switch (kind) {
        case ConstraintKind::AllowedFunctions: {
            if (!j.has("allowed") || !j["allowed"].isArray() || j["allowed"].size() == 0) {
                if (err) *err = where + " 缺少非空的 \"allowed\" 数组";
                return false;
            }
            for (size_t i = 0; i < j["allowed"].size(); ++i) {
                const Json& item = j["allowed"].at(i);
                if (!item.isString()) {
                    if (err) *err = where + " 的 \"allowed\" 中每一项都必须是字符串";
                    return false;
                }
                Op op = Op::COUNT;
                if (!opFromName(item.asString(), &op)) {
                    if (err)
                        *err = where + " 的 \"allowed\" 含未知函数名 \"" + item.asString() + "\"";
                    return false;
                }
                c.allowedOps.push_back(op);
                if (op == Op::Sub) c.allowedOps.push_back(Op::Neg);  // "-" 同时允许负号
            }
            break;
        }
        case ConstraintKind::DomainOn:
        case ConstraintKind::AnalyticOn: {
            if (!hasInterval) {
                if (err) *err = where + " 缺少 \"interval\"";
                return false;
            }
            break;
        }
        case ConstraintKind::BoundaryValue: {
            if (!readNumField(j, "point", &c.point, err, where, true)) return false;
            if (!readNumField(j, "value", &c.value, err, where, true)) return false;
            break;
        }
        case ConstraintKind::RangeOn: {
            if (!hasInterval) {
                if (err) *err = where + " 缺少 \"interval\"";
                return false;
            }
            if (j.has("bounds") && j["bounds"].isArray() && j["bounds"].size() == 2) {
                if (!jsonToNumber(j["bounds"].at(0), &c.lo, err, where + " 的下界")) return false;
                if (!jsonToNumber(j["bounds"].at(1), &c.hi, err, where + " 的上界")) return false;
            } else {
                if (!readNumField(j, "lo", &c.lo, err, where, true)) return false;
                if (!readNumField(j, "hi", &c.hi, err, where, true)) return false;
            }
            if (c.lo > c.hi) {
                if (err) *err = where + " 的下界大于上界";
                return false;
            }
            break;
        }
        case ConstraintKind::PositiveOn: {
            if (!hasInterval) {
                if (err) *err = where + " 缺少 \"interval\"";
                return false;
            }
            break;
        }
        case ConstraintKind::Monotonic: {
            if (!hasInterval) {
                if (err) *err = where + " 缺少 \"interval\"";
                return false;
            }
            if (j.has("direction") && j["direction"].isString()) {
                const std::string d = j["direction"].asString();
                if (d == "increasing" || d == "inc" || d == "up" || d == "递增") {
                    c.increasing = true;
                } else if (d == "decreasing" || d == "dec" || d == "down" || d == "递减") {
                    c.increasing = false;
                } else {
                    if (err)
                        *err = where + " 的 \"direction\" 必须是 increasing 或 decreasing";
                    return false;
                }
            } else if (j.has("increasing")) {
                c.increasing = j["increasing"].asBool(true);
            }
            c.strict = j.has("strict") ? j["strict"].asBool(false) : false;
            break;
        }
        case ConstraintKind::Parity: {
            if (j.has("parity") && j["parity"].isString()) {
                const std::string p = j["parity"].asString();
                if (p == "even" || p == "偶" || p == "偶函数") {
                    c.even = true;
                } else if (p == "odd" || p == "奇" || p == "奇函数") {
                    c.even = false;
                } else {
                    if (err) *err = where + " 的 \"parity\" 必须是 even 或 odd";
                    return false;
                }
            } else if (j.has("even")) {
                c.even = j["even"].asBool(true);
            } else if (type == "even") {
                c.even = true;
            } else if (type == "odd") {
                c.even = false;
            }
            break;
        }
        case ConstraintKind::Periodic: {
            if (j.has("period")) {
                if (!readNumField(j, "period", &c.value, err, where, true)) return false;
            } else if (j.has("value")) {
                if (!readNumField(j, "value", &c.value, err, where, true)) return false;
            } else {
                if (err) *err = where + " 缺少 \"period\"";
                return false;
            }
            if (!(c.value > 0.0)) {
                if (err) *err = where + " 的周期必须为正数";
                return false;
            }
            break;
        }
        case ConstraintKind::Asymptote: {
            if (!readNumField(j, "value", &c.value, err, where, true)) return false;
            if (j.has("side") && j["side"].isString()) {
                const std::string s = j["side"].asString();
                if (s == "both" || s == "两侧" || s == "双侧") {
                    c.asymptoteSign = 0;
                } else if (s == "pos" || s == "positive" || s == "+inf" || s == "right" ||
                           s == "+") {
                    c.asymptoteSign = 1;
                } else if (s == "neg" || s == "negative" || s == "-inf" || s == "left" ||
                           s == "-") {
                    c.asymptoteSign = -1;
                } else {
                    if (err) *err = where + " 的 \"side\" 必须是 both/pos/neg";
                    return false;
                }
            } else if (j.has("sign")) {
                c.asymptoteSign = j["sign"].asInt(0);
            } else if (j.has("asymptote_sign")) {
                c.asymptoteSign = j["asymptote_sign"].asInt(0);
            }
            if (c.asymptoteSign < -1 || c.asymptoteSign > 1) {
                if (err) *err = where + " 的渐近线方向参数必须在 -1..1";
                return false;
            }
            break;
        }
        case ConstraintKind::Extremum: {
            if (!readNumField(j, "point", &c.point, err, where, true)) return false;
            if (j.has("extremum") && j["extremum"].isString()) {
                const std::string s = j["extremum"].asString();
                if (s == "max" || s == "maximum" || s == "极大") {
                    c.isMax = true;
                } else if (s == "min" || s == "minimum" || s == "极小") {
                    c.isMax = false;
                } else {
                    if (err) *err = where + " 的 \"extremum\" 必须是 max 或 min";
                    return false;
                }
            } else if (j.has("is_max")) {
                c.isMax = j["is_max"].asBool(true);
            }
            break;
        }
        case ConstraintKind::SymmetryAxis: {
            if (j.has("axis")) {
                if (!readNumField(j, "axis", &c.point, err, where, true)) return false;
            } else if (!readNumField(j, "point", &c.point, err, where, true)) {
                return false;
            }
            break;
        }
        case ConstraintKind::ZeroCount: {
            if (!hasInterval) {
                if (err) *err = where + " 缺少 \"interval\"";
                return false;
            }
            if (!readIntFieldAny(j, {"count", "zeros"}, &c.count, err, where, true)) return false;
            if (c.count < 0) {
                if (err) *err = where + " 的零点个数不能为负";
                return false;
            }
            break;
        }
        case ConstraintKind::ComplexityLimit: {
            if (!readIntFieldAny(j, {"max_func_calls", "max_calls", "limit", "max_funcs"},
                                 &c.maxFuncCalls, err, where, true))
                return false;
            if (c.maxFuncCalls < 0) {
                if (err) *err = where + " 的函数调用上限不能为负";
                return false;
            }
            break;
        }
        case ConstraintKind::PeriodicSymmetry: {
            if (!readNumField(j, "point", &c.point, err, where, true)) return false;
            if (!readNumField(j, "value", &c.value, err, where, true)) return false;
            break;
        }

        // ---------------- 复变约束 ----------------
        case ConstraintKind::HolomorphicOn:
        case ConstraintKind::NoSingularityIn: {
            if (!j.has("region")) {
                if (err) *err = where + " 缺少 \"region\"";
                return false;
            }
            if (!parseComplexRegion(j["region"], &c.region, err, where)) return false;
            break;
        }
        case ConstraintKind::PoleOrderAt: {
            if (!j.has("point")) {
                if (err) *err = where + " 缺少 \"point\"";
                return false;
            }
            if (!parseComplexValue(j["point"], &c.cpoint, err, where + " 的 point")) return false;
            c.order = j.has("order") ? j["order"].asInt(1) : 1;
            if (c.order < 1) {
                if (err) *err = where + " 的 \"order\" 必须 >= 1";
                return false;
            }
            break;
        }
        case ConstraintKind::ResidueAt: {
            if (!j.has("point") || !j.has("value")) {
                if (err) *err = where + " 缺少 \"point\" 或 \"value\"";
                return false;
            }
            if (!parseComplexValue(j["point"], &c.cpoint, err, where + " 的 point")) return false;
            if (!parseComplexValue(j["value"], &c.cvalue, err, where + " 的 value")) return false;
            break;
        }
        case ConstraintKind::ComplexBoundaryValue: {
            if (!j.has("point") || !j.has("value")) {
                if (err) *err = where + " 缺少 \"point\" 或 \"value\"";
                return false;
            }
            if (!parseComplexValue(j["point"], &c.cpoint, err, where + " 的 point")) return false;
            if (!parseComplexValue(j["value"], &c.cvalue, err, where + " 的 value")) return false;
            break;
        }
        case ConstraintKind::ContourIntegralValue: {
            if (j.has("center")) {
                if (!parseComplexValue(j["center"], &c.cpoint, err, where + " 的 center")) return false;
            }
            c.radius = j.has("radius") ? j["radius"].asNumber(1.0) : 1.0;
            if (!(c.radius > 0.0)) {
                if (err) *err = where + " 的 \"radius\" 必须为正";
                return false;
            }
            if (!j.has("value")) {
                if (err) *err = where + " 缺少 \"value\"";
                return false;
            }
            if (!parseComplexValue(j["value"], &c.cvalue, err, where + " 的 value")) return false;
            break;
        }
        case ConstraintKind::ZeroCountInRegion: {
            if (!j.has("region")) {
                if (err) *err = where + " 缺少 \"region\"";
                return false;
            }
            if (!parseComplexRegion(j["region"], &c.region, err, where)) return false;
            c.count = j.has("count") ? j["count"].asInt(0)
                                     : (j.has("zeros") ? j["zeros"].asInt(0) : 0);
            if (c.count < 0) {
                if (err) *err = where + " 的 \"count\" 不能为负";
                return false;
            }
            break;
        }
        case ConstraintKind::ModulusRangeOn: {
            if (!j.has("region")) {
                if (err) *err = where + " 缺少 \"region\"";
                return false;
            }
            if (!parseComplexRegion(j["region"], &c.region, err, where)) return false;
            if (j.has("bounds") && j["bounds"].isArray() && j["bounds"].size() == 2) {
                if (!jsonToNumber(j["bounds"].at(0), &c.lo, err, where + " 的下界")) return false;
                if (!jsonToNumber(j["bounds"].at(1), &c.hi, err, where + " 的上界")) return false;
            } else {
                if (!readNumField(j, "lo", &c.lo, err, where, true)) return false;
                if (!readNumField(j, "hi", &c.hi, err, where, true)) return false;
            }
            if (c.lo > c.hi) {
                if (err) *err = where + " 的下界大于上界";
                return false;
            }
            break;
        }
    }

    *out = c;
    return true;
}

}  // namespace

// -----------------------------------------------------------------
// 关卡合法性检查：所有约束都可判定、数据自洽
// -----------------------------------------------------------------
bool LevelDef::valid(std::string* err) const {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (id.empty()) return fail("关卡 id 为空");
    if (chapter < 1) return fail("关卡 " + id + " 的章节号必须 >= 1");
    if (vars.empty()) return fail("关卡 " + id + " 没有声明变量");
    if (constraints.empty()) return fail("关卡 " + id + " 没有任何约束");
    if (!std::isfinite(timeLimit) || timeLimit < 0.0)
        return fail("关卡 " + id + " 的限时值无效");
    if (!std::isfinite(parScore) || parScore < 0.0 || parScore > 100.0)
        return fail("关卡 " + id + " 的 parScore 必须在 0..100");

    for (size_t i = 0; i < constraints.size(); ++i) {
        const Constraint& c = constraints[i];
        const std::string where = "关卡 " + id + " 的第 " + std::to_string(i + 1) +
                                  " 条约束（" + constraintKindDisplay(c.kind) + "）";
        if (!constraintKindSupported(c.kind)) return fail(where + " 类型不受支持");
        if (c.var.empty()) return fail(where + " 的作用变量为空");
        switch (c.kind) {
            case ConstraintKind::AllowedFunctions:
                if (c.allowedOps.empty()) return fail(where + " 的允许函数列表为空");
                break;
            case ConstraintKind::DomainOn:
            case ConstraintKind::AnalyticOn:
            case ConstraintKind::PositiveOn:
            case ConstraintKind::Monotonic:
            case ConstraintKind::RangeOn:
            case ConstraintKind::ZeroCount:
                if (c.interval.isEmpty()) return fail(where + " 的区间为空");
                break;
            default:
                break;
        }
        if (c.kind == ConstraintKind::RangeOn || c.kind == ConstraintKind::BoundaryValue ||
            c.kind == ConstraintKind::Periodic || c.kind == ConstraintKind::Asymptote ||
            c.kind == ConstraintKind::Extremum || c.kind == ConstraintKind::SymmetryAxis ||
            c.kind == ConstraintKind::PeriodicSymmetry) {
            if (!std::isfinite(c.value) && c.kind != ConstraintKind::Extremum &&
                c.kind != ConstraintKind::SymmetryAxis) {
                return fail(where + " 的数值参数不是有限值");
            }
        }
        if (c.kind == ConstraintKind::RangeOn && c.lo > c.hi)
            return fail(where + " 的下界大于上界");
        if (c.kind == ConstraintKind::Periodic && !(c.value > 0.0))
            return fail(where + " 的周期必须为正数");
        if (c.kind == ConstraintKind::Asymptote && (c.asymptoteSign < -1 || c.asymptoteSign > 1))
            return fail(where + " 的渐近线方向参数无效");
        if (c.kind == ConstraintKind::ZeroCount && c.count < 0)
            return fail(where + " 的零点个数不能为负");
        if (c.kind == ConstraintKind::ComplexityLimit && c.maxFuncCalls < 0)
            return fail(where + " 的函数调用上限不能为负");
        if (c.kind == ConstraintKind::BoundaryValue || c.kind == ConstraintKind::Extremum ||
            c.kind == ConstraintKind::SymmetryAxis || c.kind == ConstraintKind::PeriodicSymmetry) {
            if (!std::isfinite(c.point)) return fail(where + " 的取点位置不是有限值");
        }
    }

    if (!reference.empty()) {
        ParseError pe;
        ExprP ref = parseExpr(reference, &pe);
        if (!ref) return fail("关卡 " + id + " 的参考解无法解析：" + pe.message);
        for (const std::string& v : collectVars(ref)) {
            if (std::find(vars.begin(), vars.end(), v) == vars.end())
                return fail("关卡 " + id + " 的参考解使用了未声明的变量 \"" + v + "\"");
        }
    }
    if (err) err->clear();
    return true;
}

// -----------------------------------------------------------------
// 从 JSON 解析单个关卡
// -----------------------------------------------------------------
LevelDef levelFromJson(const Json& j, std::string* err) {
    LevelDef L;
    std::string localErr;
    std::string* e = err ? err : &localErr;
    e->clear();

    if (!j.isObject()) {
        *e = "关卡必须是 JSON 对象";
        return L;
    }
    if (!j.has("id") || !j["id"].isString() || j["id"].asString().empty()) {
        *e = "关卡缺少字符串字段 \"id\"";
        return L;
    }
    L.id = j["id"].asString();
    if (j.has("title")) L.title = j["title"].asString();
    if (j.has("hint")) L.hint = j["hint"].asString();
    if (j.has("intro")) L.intro = j["intro"].asString();
    if (j.has("chapter")) L.chapter = j["chapter"].asInt(1);
    if (j.has("reference")) L.reference = j["reference"].asString();
    if (j.has("time_limit")) {
        double t = 0.0;
        if (!readNumField(j, "time_limit", &t, e, "关卡 " + L.id, false, 0.0)) return L;
        L.timeLimit = std::max(0.0, t);
    }
    {
        double p = 85.0;
        if (!readNumFieldAny(j, {"parScore", "par_score"}, &p, e, "关卡 " + L.id, false, 85.0))
            return L;
        L.parScore = p;
    }
    if (j.has("domain") && j["domain"].isString()) {
        const std::string d = j["domain"].asString();
        if (d == "complex" || d == "Complex" || d == "复数" || d == "复变") {
            L.domain = DomainKind::Complex;
        } else if (d == "real" || d == "Real" || d == "实数") {
            L.domain = DomainKind::Real;
        } else {
            *e = "关卡 " + L.id + " 的 \"domain\" 必须是 real 或 complex";
            return L;
        }
    }
    if (j.has("vars") && j["vars"].isArray() && j["vars"].size() > 0) {
        L.vars.clear();
        for (size_t i = 0; i < j["vars"].size(); ++i) {
            if (!j["vars"].at(i).isString()) {
                *e = "关卡 " + L.id + " 的 \"vars\" 必须是字符串数组";
                return L;
            }
            L.vars.push_back(j["vars"].at(i).asString());
        }
    }
    if (!j.has("constraints") || !j["constraints"].isArray()) {
        *e = "关卡 " + L.id + " 缺少 \"constraints\" 数组";
        return L;
    }
    L.constraints.clear();
    for (size_t i = 0; i < j["constraints"].size(); ++i) {
        Constraint c;
        const std::string where =
            "关卡 " + L.id + " 的第 " + std::to_string(i + 1) + " 条约束";
        if (!constraintFromJson(j["constraints"].at(i), &c, e, where)) {
            L.constraints.clear();
            return L;
        }
        L.constraints.push_back(c);
    }
    // 复数关卡：未显式声明变量时默认用 z
    if (L.isComplex() && L.vars.size() == 1 && L.vars[0] == "x" &&
        !(j.has("vars") && j["vars"].isArray() && j["vars"].size() > 0)) {
        L.vars[0] = "z";
    }
    // 约束未显式指定变量时，跟随关卡主变量
    {
        const std::string mainVar = L.primaryVar();
        for (auto& c : L.constraints) {
            if (c.var.empty() || c.var == "x") c.var = mainVar;
        }
    }
    std::string verr;
    if (!L.valid(&verr)) {
        *e = verr;
        return L;
    }
    e->clear();
    return L;
}

// -----------------------------------------------------------------
// 从文件加载关卡数组
// -----------------------------------------------------------------
std::vector<LevelDef> loadLevels(const std::string& path, std::string* err) {
    std::vector<LevelDef> out;
    std::string perr;
    const Json root = Json::parseFile(path, &perr);
    if (!perr.empty()) {
        if (err) *err = "无法读取关卡文件 " + path + "：" + perr;
        return out;
    }
    if (!root.isArray()) {
        if (err) *err = "关卡文件 " + path + " 的根节点必须是 JSON 数组";
        return out;
    }
    if (root.size() == 0) {
        if (err) *err = "关卡文件 " + path + " 中没有关卡";
        return out;
    }
    for (size_t i = 0; i < root.size(); ++i) {
        std::string lerr;
        LevelDef l = levelFromJson(root.at(i), &lerr);
        if (!lerr.empty()) {
            if (err) *err = "第 " + std::to_string(i + 1) + " 关解析失败：" + lerr;
            out.clear();
            return out;
        }
        out.push_back(std::move(l));
    }
    if (err) err->clear();
    return out;
}

}  // namespace fcg
