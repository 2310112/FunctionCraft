// Parser.h — 表达式解析器（计算器式输入 → AST）
#pragma once

#include <string>

#include "Expr.h"

namespace fcg {

struct ParseError {
    int pos = 0;            // 出错位置（0-based 字符下标）
    std::string message;    // 中文错误说明
    bool ok() const { return message.empty(); }
};

// 解析表达式；失败返回 nullptr 并填充 err
ExprP parseExpr(const std::string& src, ParseError* err = nullptr);

// 语法检查（不构造 AST，快速用于实时输入反馈）
bool validateExpr(const std::string& src, ParseError* err = nullptr);

// 该标识符是否是已知函数名
bool isKnownFunction(const std::string& name);
// 该标识符是否是已知常量名
bool isKnownConstant(const std::string& name);

// 计算器按键 → 插入文本（供 UI 使用）
// 返回插入到光标处的字符串，例如 "sin(" 、"^" 、"pi"
std::string tokenForButton(const std::string& buttonId);

}  // namespace fcg
