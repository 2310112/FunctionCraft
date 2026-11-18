// Domain.cpp — 待实现（由 engine-analysis 负责）
#include "Domain.h"
namespace fcg {
Interval Interval::all() { Interval i; return i; }
Interval Interval::closed(double a, double b) { Interval i; i.lo=a; i.hi=b; i.loInf=i.hiInf=false; i.loClosed=i.hiClosed=true; return i; }
Interval Interval::open(double a, double b) { Interval i; i.lo=a; i.hi=b; i.loInf=i.hiInf=false; i.loClosed=i.hiClosed=false; return i; }
bool Interval::isEmpty() const { return !loInf && !hiInf && lo > hi; }
bool Interval::contains(double) const { return true; }
double Interval::length() const { return 0; }
std::string Interval::str() const { return "[?]"; }
bool operator==(const Interval&, const Interval&) { return true; }
bool DomainSet::contains(double) const { return true; }
bool DomainSet::containsAll(const Interval&) const { return true; }
std::string DomainSet::str() const { return "R"; }
void DomainSet::normalize(double) {}
DomainSet DomainSet::whole() { DomainSet d; d.parts.push_back(Interval::all()); return d; }
DomainSet domainOf(const ExprP&, const std::string&) { return DomainSet::whole(); }
std::vector<double> singularitiesIn(const ExprP&, const Interval&, const std::string&) { return {}; }
bool isAnalyticOn(const ExprP&, const Interval&, const std::string&, std::string*) { return true; }
bool isBuiltFromAnalyticOps(const ExprP& e, Op*) { return e && opIsAnalytic(e->op); }
RangeResult rangeOn(const ExprP&, const Interval&, const std::string&) { RangeResult r; r.ok=false; r.error="未实现"; return r; }
std::vector<double> zerosIn(const ExprP&, const Interval&, const std::string&) { return {}; }
MonotonicResult monotonicOn(const ExprP&, const Interval&, const std::string&) { MonotonicResult r; r.ok=false; r.error="未实现"; return r; }
}  // namespace fcg
