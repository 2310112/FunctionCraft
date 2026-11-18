// Domain.h — 区间、定义域分析、解析性判定
#pragma once

#include <string>
#include <vector>

#include "Expr.h"

namespace fcg {

// 闭/开区间（支持 ±∞）
struct Interval {
    double lo = -1e308;  // 用大数表示 ∞，配合 isInfiniteLo/Hi
    double hi = 1e308;
    bool loClosed = true;
    bool hiClosed = true;
    bool loInf = true;
    bool hiInf = true;

    static Interval all();
    static Interval closed(double a, double b);
    static Interval open(double a, double b);

    bool isEmpty() const;
    bool contains(double x) const;
    double length() const;  // ∞ 时返回大数
    std::string str() const;
};

bool operator==(const Interval& a, const Interval& b);

// 定义域 = 若干区间的并
struct DomainSet {
    std::vector<Interval> parts;

    bool empty() const { return parts.empty(); }
    bool contains(double x) const;
    bool containsAll(const Interval& iv) const;  // iv 是否为子集
    std::string str() const;
    void normalize(double gapMerge = 1e-9);  // 合并重叠区间
    static DomainSet whole();
};

// 表达式在实数域上的定义域（符号推导 + 数值校验的混合策略）
DomainSet domainOf(const ExprP& e, const std::string& var = "x");

// 表达式中出现的"奇点/不可导点"（如 1/x 在 0、tan 在 π/2）
std::vector<double> singularitiesIn(const ExprP& e, const Interval& iv,
                                    const std::string& var = "x");

// 是否在 iv 上解析：1) 无非解析函数；2) iv 内无奇点
bool isAnalyticOn(const ExprP& e, const Interval& iv, const std::string& var,
                  std::string* reason);

// 表达式是否只由解析函数构成（不看奇点）
bool isBuiltFromAnalyticOps(const ExprP& e, Op* firstBadOp = nullptr);

// 求表达式在区间上的取值范围（数值扫描 + 符号边界）
struct RangeResult {
    bool ok = false;
    double lo = 0.0, hi = 0.0;
    std::string error;
};
RangeResult rangeOn(const ExprP& e, const Interval& iv, const std::string& var = "x");

// 零点的近似位置（数值扫描 + 二分细化）
std::vector<double> zerosIn(const ExprP& e, const Interval& iv, const std::string& var = "x");

// 单调性：在 iv 上是否单调（用导数符号数值判定 + 符号快路径）
struct MonotonicResult {
    bool ok = false;
    bool increasing = false;   // true: 单调增
    bool strictly = false;
    std::string error;
};
MonotonicResult monotonicOn(const ExprP& f, const Interval& iv, const std::string& var = "x");

}  // namespace fcg
