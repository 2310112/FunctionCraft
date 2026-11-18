// test_core.cpp — AST / 求值 / 解析 / JSON 的测试
#include "test_framework.h"

#include "core/Expr.h"
#include "core/Json.h"
#include "core/Numeric.h"
#include "core/Parser.h"

using namespace fcg;

namespace {

ExprP P(const std::string& s) {
    ParseError err;
    ExprP e = parseExpr(s, &err);
    if (!e) {
        ::fcgtest::fail("解析失败: " + s + " -> " + err.message, __FILE__, __LINE__);
    }
    return e;
}

double V(ExprP e, double x) {
    auto r = evalReal(e, "x", x);
    if (!r.ok) ::fcgtest::fail("求值失败: " + e->toStr() + " -> " + r.error, __FILE__, __LINE__);
    return r.value;
}

}  // namespace

FCG_TEST(parse_basic) {
    CHECK_NEAR(V(P("1 + 2*3"), 0.0), 7.0, 1e-12);
    CHECK_NEAR(V(P("(1+2)*3"), 0.0), 9.0, 1e-12);
    CHECK_NEAR(V(P("2^3^2"), 0.0), 512.0, 1e-12);  // 右结合
    CHECK_NEAR(V(P("-x^2"), 3.0), -9.0, 1e-12);    // -(x^2)
    CHECK_NEAR(V(P("2x"), 5.0), 10.0, 1e-12);      // 隐式乘法
    CHECK_NEAR(V(P("3sin(x)"), 0.0), 0.0, 1e-12);
    CHECK_NEAR(V(P("2(x+1)"), 3.0), 8.0, 1e-12);
}

FCG_TEST(parse_functions) {
    CHECK_NEAR(V(P("sin(pi/2)"), 0.0), 1.0, 1e-12);
    CHECK_NEAR(V(P("cos(0)"), 0.0), 1.0, 1e-12);
    CHECK_NEAR(V(P("exp(0)"), 0.0), 1.0, 1e-12);
    CHECK_NEAR(V(P("ln(e)"), 0.0), 1.0, 1e-12);
    CHECK_NEAR(V(P("sqrt(9)"), 0.0), 3.0, 1e-12);
    CHECK_NEAR(V(P("abs(-3)"), 0.0), 3.0, 1e-12);
    CHECK_NEAR(V(P("log10(1000)"), 0.0), 3.0, 1e-12);
    CHECK_NEAR(V(P("log2(8)"), 0.0), 3.0, 1e-12);
    CHECK_NEAR(V(P("tanh(0)"), 0.0), 0.0, 1e-12);
    CHECK_NEAR(V(P("max(2,7)"), 0.0), 7.0, 1e-12);
    CHECK_NEAR(V(P("mod(7,3)"), 0.0), 1.0, 1e-12);
}

FCG_TEST(parse_errors) {
    ParseError err;
    CHECK(parseExpr("sin(", &err) == nullptr);
    CHECK(!err.ok());
    CHECK(parseExpr("1+", &err) == nullptr);
    CHECK(parseExpr("(1+2", &err) == nullptr);
    CHECK(parseExpr("", &err) == nullptr);
}

FCG_TEST(to_string_roundtrip) {
    const char* cases[] = {"sin(x)", "2x + 1", "x^2", "exp(-x^2)", "sin(x)*cos(x)"};
    for (const char* c : cases) {
        ExprP e = P(c);
        std::string s = e->toStr();
        ParseError err;
        ExprP e2 = parseExpr(s, &err);
        CHECK(e2 != nullptr);
        // 数值上应一致
        for (double x : {-1.3, 0.0, 2.7}) {
            CHECK_NEAR(V(e, x), V(e2, x), 1e-12);
        }
    }
}

FCG_TEST(eval_domain_errors) {
    auto r1 = evalReal(P("ln(x)"), "x", -1.0);
    CHECK(!r1.ok);
    auto r2 = evalReal(P("1/x"), "x", 0.0);
    CHECK(!r2.ok);
    auto r3 = evalReal(P("sqrt(x)"), "x", -1.0);
    CHECK(!r3.ok);
}

FCG_TEST(complex_eval) {
    VarMap m;
    auto e = P("sqrt(-1)");
    auto r = evaluate(e, m);
    CHECK(r.ok);
    CHECK_NEAR(r.value.re, 0.0, 1e-12);
    CHECK_NEAR(r.value.im, 1.0, 1e-12);
}

FCG_TEST(ast_tools) {
    ExprP e = P("sin(x) + cos(2x)");
    CHECK_EQ(funcCallCount(e), 2);
    CHECK_EQ(collectVars(e).size(), size_t(1));
    CHECK(isConst(P("1+pi")));
    CHECK(!isConst(P("x+1")));
}

FCG_TEST(json_basic) {
    std::string err;
    Json j = Json::parse(R"({"a":1,"b":[1,2,3],"c":{"d":"中文"},"e":true})", &err);
    CHECK(err.empty());
    CHECK_EQ(j["a"].asInt(), 1);
    CHECK_EQ(j["b"].size(), size_t(3));
    CHECK_EQ(j["c"]["d"].asString(), std::string("中文"));
    CHECK(j["e"].asBool());
    Json bad = Json::parse("{", &err);
    CHECK(!err.empty());
}
