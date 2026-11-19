// Parser.cpp — 词法分析 + 递归下降解析
#include "Parser.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <string>

namespace fcg {

namespace {

struct Token {
    enum class Type { Number, Ident, Op, LParen, RParen, Comma, End, Invalid };
    Type type = Type::End;
    double num = 0.0;
    std::string text;
    int pos = 0;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : s_(src) {}

    Token next() {
        skipSpace();
        Token t;
        t.pos = static_cast<int>(pos_);
        if (pos_ >= s_.size()) {
            t.type = Token::Type::End;
            return t;
        }
        char c = s_[pos_];

        // 数字
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && pos_ + 1 < s_.size() &&
             std::isdigit(static_cast<unsigned char>(s_[pos_ + 1])))) {
            return number();
        }

        // 标识符
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = pos_;
            while (pos_ < s_.size() &&
                   (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_'))
                ++pos_;
            t.type = Token::Type::Ident;
            t.text = s_.substr(start, pos_ - start);
            return t;
        }

        // UTF-8 特殊字符
        if (c == '\xCF' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '\x80') {  // π (U+03C0)
            pos_ += 2;
            t.type = Token::Type::Ident;
            t.text = "pi";
            return t;
        }
        if (c == '\xE2' && pos_ + 2 < s_.size()) {
            unsigned char b1 = static_cast<unsigned char>(s_[pos_ + 1]);
            unsigned char b2 = static_cast<unsigned char>(s_[pos_ + 2]);
            if (b1 == 0x88 && b2 == 0x9A) {  // √ U+221A
                pos_ += 3;
                t.type = Token::Type::Ident;
                t.text = "sqrt";
                return t;
            }
            if (b1 == 0x88 && b2 == 0x92) {  // − U+2212
                pos_ += 3;
                t.type = Token::Type::Op;
                t.text = "-";
                return t;
            }
            if (b1 == 0x9C && b2 == 0x89) {  // ❌ skip
                pos_ += 3;
                return next();
            }
        }
        if (c == '\xC2' && pos_ + 1 < s_.size() &&
            static_cast<unsigned char>(s_[pos_ + 1]) == 0xB7) {  // · U+00B7 乘号
            pos_ += 2;
            t.type = Token::Type::Op;
            t.text = "*";
            return t;
        }
        if (c == '\xC3' && pos_ + 1 < s_.size()) {
            unsigned char b1 = static_cast<unsigned char>(s_[pos_ + 1]);
            if (b1 == 0x97) {  // × U+00D7
                pos_ += 2;
                t.type = Token::Type::Op;
                t.text = "*";
                return t;
            }
            if (b1 == 0xB7) {  // ÷ U+00F7
                pos_ += 2;
                t.type = Token::Type::Op;
                t.text = "/";
                return t;
            }
        }

        switch (c) {
            case '+':
            case '-':
            case '*':
            case '/':
            case '^':
                ++pos_;
                t.type = Token::Type::Op;
                t.text = std::string(1, c);
                // ** 视为 ^
                if (c == '*' && pos_ < s_.size() && s_[pos_] == '*') {
                    ++pos_;
                    t.text = "^";
                }
                return t;
            case '(':
            case '（':
                ++pos_;
                t.type = Token::Type::LParen;
                return t;
            case ')':
            case '）':
                ++pos_;
                t.type = Token::Type::RParen;
                return t;
            case ',':
            case '，':
                ++pos_;
                t.type = Token::Type::Comma;
                return t;
            default:
                break;
        }

        // 未知字符：跳过一个 UTF-8 字符
        int len = 1;
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 0xF0) len = 4;
        else if (uc >= 0xE0) len = 3;
        else if (uc >= 0xC0) len = 2;
        pos_ += len;
        t.type = Token::Type::Invalid;
        t.text = s_.substr(t.pos, len);
        return t;
    }

private:
    void skipSpace() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n'))
            ++pos_;
    }

    Token number() {
        Token t;
        t.type = Token::Type::Number;
        t.pos = static_cast<int>(pos_);
        size_t start = pos_;
        bool seenDot = false;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                ++pos_;
            } else if (c == '.' && !seenDot) {
                seenDot = true;
                ++pos_;
            } else {
                break;
            }
        }
        // 指数部分：仅当后面确实跟着数字
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            size_t save = pos_;
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
            } else {
                pos_ = save;
            }
        }
        t.text = s_.substr(start, pos_ - start);
        t.num = std::strtod(t.text.c_str(), nullptr);
        return t;
    }

    const std::string& s_;
    size_t pos_ = 0;
};

// 函数名 → Op（含别名）
struct FuncEntry {
    Op op;
    int arity;  // -1 = 可变（需要显式处理）
};

const std::map<std::string, FuncEntry>& funcTable() {
    static const std::map<std::string, FuncEntry> table = {
        {"sin", {Op::Sin, 1}},     {"cos", {Op::Cos, 1}},     {"tan", {Op::Tan, 1}},
        {"exp", {Op::Exp, 1}},     {"ln", {Op::Ln, 1}},       {"log", {Op::Ln, 1}},
        {"log2", {Op::Log2, 1}},   {"lg", {Op::Log10, 1}},    {"log10", {Op::Log10, 1}},
        {"sqrt", {Op::Sqrt, 1}},   {"cbrt", {Op::Cbrt, 1}},   {"abs", {Op::Abs, 1}},
        {"asin", {Op::Asin, 1}},   {"arcsin", {Op::Asin, 1}},
        {"acos", {Op::Acos, 1}},   {"arccos", {Op::Acos, 1}},
        {"atan", {Op::Atan, 1}},   {"arctan", {Op::Atan, 1}},
        {"sinh", {Op::Sinh, 1}},   {"cosh", {Op::Cosh, 1}},   {"tanh", {Op::Tanh, 1}},
        {"asinh", {Op::Asinh, 1}}, {"acosh", {Op::Acosh, 1}}, {"atanh", {Op::Atanh, 1}},
        {"floor", {Op::Floor, 1}}, {"ceil", {Op::Ceil, 1}},   {"sign", {Op::Sign, 1}},
        {"sgn", {Op::Sign, 1}},    {"min", {Op::Min, 2}},     {"max", {Op::Max, 2}},
        {"mod", {Op::Mod, 2}},
        // 复数域
        {"re", {Op::Re, 1}},       {"real", {Op::Re, 1}},
        {"im", {Op::Im, 1}},       {"imag", {Op::Im, 1}},
        {"conj", {Op::Conj, 1}},   {"arg", {Op::Arg, 1}},
    };
    return table;
}

class Parser {
public:
    explicit Parser(const std::string& src) : lex_(src) { advance(); }

    ExprP parse(ParseError* err) {
        ExprP e = expr();
        if (failed_) {
            if (err) *err = err_;
            return nullptr;
        }
        if (tok_.type != Token::Type::End) {
            setError(tok_.pos, "表达式结尾有多余符号 '" + tok_.text + "'");
            if (err) *err = err_;
            return nullptr;
        }
        return e;
    }

private:
    void advance() { tok_ = lex_.next(); }

    void setError(int pos, std::string msg) {
        if (failed_) return;
        failed_ = true;
        err_.pos = pos;
        err_.message = std::move(msg);
    }

    bool acceptOp(const char* op) {
        if (tok_.type == Token::Type::Op && tok_.text == op) {
            advance();
            return true;
        }
        return false;
    }

    ExprP expr() {
        ExprP e = addsub();
        return e;
    }

    ExprP addsub() {
        ExprP left = muldiv();
        while (!failed_) {
            if (acceptOp("+")) {
                ExprP r = muldiv();
                if (!r) return nullptr;
                left = mkAdd(left, r);
            } else if (acceptOp("-")) {
                ExprP r = muldiv();
                if (!r) return nullptr;
                left = mkSub(left, r);
            } else {
                break;
            }
        }
        return left;
    }

    bool startsAtom() const {
        switch (tok_.type) {
            case Token::Type::Number:
            case Token::Type::Ident:
            case Token::Type::LParen:
                return true;
            default:
                return false;
        }
    }

    ExprP muldiv() {
        ExprP left = unary();
        while (!failed_) {
            if (acceptOp("*")) {
                ExprP r = unary();
                if (!r) return nullptr;
                left = mkMul(left, r);
            } else if (acceptOp("/")) {
                ExprP r = unary();
                if (!r) return nullptr;
                left = mkDiv(left, r);
            } else if (startsAtom()) {
                // 隐式乘法：2x, 3sin(x), 2(x+1)
                ExprP r = unary();
                if (!r) return nullptr;
                left = mkMul(left, r);
            } else {
                break;
            }
        }
        return left;
    }

    ExprP unary() {
        if (acceptOp("-")) {
            ExprP a = unary();
            if (!a) return nullptr;
            return mkNeg(a);
        }
        if (acceptOp("+")) {
            return unary();
        }
        return power();
    }

    ExprP power() {
        ExprP base = atom();
        if (failed_) return nullptr;
        if (acceptOp("^")) {
            ExprP exp = unary();  // 右结合，允许 -1
            if (!exp) return nullptr;
            return mkPow(base, exp);
        }
        return base;
    }

    ExprP atom() {
        switch (tok_.type) {
            case Token::Type::Number: {
                ExprP e = mkNum(tok_.num);
                advance();
                return e;
            }
            case Token::Type::LParen: {
                advance();
                ExprP e = expr();
                if (failed_) return nullptr;
                if (tok_.type != Token::Type::RParen) {
                    setError(tok_.pos, "缺少右括号 ')'");
                    return nullptr;
                }
                advance();
                return e;
            }
            case Token::Type::Ident: {
                std::string name = tok_.text;
                int pos = tok_.pos;
                advance();
                // 函数名大小写不敏感：Re(z) 与 re(z) 等价
                std::string lname = name;
                for (char& ch : lname) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                // 函数调用
                const auto& tbl = funcTable();
                auto it = tbl.find(lname);
                if (it != tbl.end()) {
                    if (tok_.type != Token::Type::LParen) {
                        setError(pos, "函数 " + name + " 后需要括号");
                        return nullptr;
                    }
                    advance();
                    ExprP a1 = expr();
                    if (failed_) return nullptr;
                    if (it->second.arity == 2) {
                        if (tok_.type != Token::Type::Comma) {
                            setError(tok_.pos, "函数 " + name + " 需要两个参数");
                            return nullptr;
                        }
                        advance();
                        ExprP a2 = expr();
                        if (failed_) return nullptr;
                        if (tok_.type != Token::Type::RParen) {
                            setError(tok_.pos, "缺少右括号 ')'");
                            return nullptr;
                        }
                        advance();
                        return mkFunc2(it->second.op, a1, a2);
                    }
                    if (tok_.type != Token::Type::RParen) {
                        setError(tok_.pos, "缺少右括号 ')'");
                        return nullptr;
                    }
                    advance();
                    // cot/sec/csc 用倒数实现
                    if (name == "cot") return mkDiv(mkNum(1.0), mkFunc1(Op::Tan, a1));
                    if (name == "sec") return mkDiv(mkNum(1.0), mkFunc1(Op::Cos, a1));
                    if (name == "csc") return mkDiv(mkNum(1.0), mkFunc1(Op::Sin, a1));
                    return mkFunc1(it->second.op, a1);
                }
                // 常量
                if (lname == "pi") return mkPi();
                if (lname == "e") return mkE();
                if (lname == "i") return mkI();
                if (lname == "inf") return mkNum(INFINITY);
                // 变量
                return mkVar(name);
            }
            case Token::Type::End:
                setError(tok_.pos, "表达式不完整");
                return nullptr;
            case Token::Type::Invalid:
                setError(tok_.pos, "无法识别的字符 '" + tok_.text + "'");
                return nullptr;
            default:
                setError(tok_.pos, "此处不应出现 '" + tok_.text + "'");
                return nullptr;
        }
    }

    Lexer lex_;
    Token tok_;
    bool failed_ = false;
    ParseError err_;
};

}  // namespace

ExprP parseExpr(const std::string& src, ParseError* err) {
    if (src.empty()) {
        if (err) {
            err->pos = 0;
            err->message = "表达式为空";
        }
        return nullptr;
    }
    Parser p(src);
    return p.parse(err);
}

bool validateExpr(const std::string& src, ParseError* err) {
    ParseError local;
    ExprP e = parseExpr(src, err ? err : &local);
    return e != nullptr;
}

bool isKnownFunction(const std::string& name) { return funcTable().count(name) > 0; }

bool isKnownConstant(const std::string& name) {
    return name == "pi" || name == "e" || name == "i" || name == "inf";
}

std::string tokenForButton(const std::string& id) {
    static const std::map<std::string, std::string> tokens = {
        {"sin", "sin("},   {"cos", "cos("},   {"tan", "tan("},   {"cot", "cot("},
        {"sec", "sec("},   {"csc", "csc("},   {"exp", "exp("},   {"ln", "ln("},
        {"log", "log("},   {"log2", "log2("}, {"log10", "log10("}, {"sqrt", "sqrt("},
        {"cbrt", "cbrt("}, {"abs", "abs("},   {"asin", "asin("}, {"acos", "acos("},
        {"atan", "atan("}, {"sinh", "sinh("}, {"cosh", "cosh("}, {"tanh", "tanh("},
        {"asinh", "asinh("}, {"acosh", "acosh("}, {"atanh", "atanh("},
        {"floor", "floor("}, {"ceil", "ceil("}, {"sign", "sign("},
        {"min", "min("},   {"max", "max("},   {"mod", "mod("},
        {"pi", "pi"},      {"e", "e"},        {"i", "i"},
        {"+", "+"},        {"-", "-"},        {"*", "*"},        {"/", "/"},
        {"^", "^"},        {"(", "("},        {")", ")"},        {",", ","},
        {"x", "x"},        {"y", "y"},        {"z", "z"},
    };
    auto it = tokens.find(id);
    return it == tokens.end() ? id : it->second;
}

}  // namespace fcg
