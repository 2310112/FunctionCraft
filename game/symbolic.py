"""
符号引擎核心：表达式树、解析器、求导、代入、化简、定义域分析。
设计原则：变量类型无关（实数/复数/多变量共用同一套 AST）。
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Any, Optional


# ============================================================
# 1. AST 节点
# ============================================================

class Op(Enum):
    # 叶子
    CONST = auto()      # 数字常量
    VAR = auto()        # 变量 (x, y, z, ...)
    # 一元
    NEG = auto()        # -expr
    # 二元
    ADD = auto()
    SUB = auto()
    MUL = auto()
    DIV = auto()
    POW = auto()
    # 函数
    SIN = auto()
    COS = auto()
    TAN = auto()
    EXP = auto()
    LOG = auto()        # 自然对数
    SQRT = auto()
    ABS = auto()
    ARCSIN = auto()
    ARCCOS = auto()
    ARCTAN = auto()
    SINH = auto()
    COSH = auto()
    TANH = auto()
    SEC = auto()
    CSC = auto()
    COT = auto()
    FLOOR = auto()
    CEIL = auto()
    SIGN = auto()
    # 特殊常量
    PI = auto()
    E = auto()


# 非解析函数集合（用于解析性检查）
NON_ANALYTIC_OPS = {Op.ABS, Op.FLOOR, Op.CEIL, Op.SIGN}

# 各函数的定义域限制类型
# 'pos': 参数 > 0, 'nonneg': 参数 >= 0, 'nonzero': 参数 != 0, 'none': 无限制
DOMAIN_REQUIREMENTS = {
    Op.LOG: 'pos',
    Op.SQRT: 'nonneg',
    Op.ARCSIN: 'none',   # [-1,1]，简化处理
    Op.ARCCOS: 'none',
}


@dataclass
class Expr:
    """表达式树节点。"""
    op: Op
    # 叶子
    value: float = 0.0          # CONST / PI / E
    var: str = ""               # VAR
    # 一元
    child: Optional["Expr"] = None
    # 二元
    left: Optional["Expr"] = None
    right: Optional["Expr"] = None

    def __repr__(self) -> str:
        return self.to_string()

    # ---- 工厂方法 ----
    @staticmethod
    def const(v: float) -> "Expr":
        return Expr(Op.CONST, value=v)

    @staticmethod
    def var(name: str) -> "Expr":
        return Expr(Op.VAR, var=name)

    @staticmethod
    def neg(e: "Expr") -> "Expr":
        return Expr(Op.NEG, child=e)

    @staticmethod
    def add(a: "Expr", b: "Expr") -> "Expr":
        return Expr(Op.ADD, left=a, right=b)

    @staticmethod
    def sub(a: "Expr", b: "Expr") -> "Expr":
        return Expr(Op.SUB, left=a, right=b)

    @staticmethod
    def mul(a: "Expr", b: "Expr") -> "Expr":
        return Expr(Op.MUL, left=a, right=b)

    @staticmethod
    def div(a: "Expr", b: "Expr") -> "Expr":
        return Expr(Op.DIV, left=a, right=b)

    @staticmethod
    def pow(a: "Expr", b: "Expr") -> "Expr":
        return Expr(Op.POW, left=a, right=b)

    @staticmethod
    def func(op: Op, arg: "Expr") -> "Expr":
        return Expr(op, child=arg)

    # ---- 字符串表示 ----
    def to_string(self) -> str:
        op = self.op
        if op == Op.CONST:
            v = self.value
            if v == int(v) and abs(v) < 1e15:
                return str(int(v))
            return f"{v:.6g}"
        if op == Op.PI:
            return "π"
        if op == Op.E:
            return "e"
        if op == Op.VAR:
            return self.var
        if op == Op.NEG:
            s = self.child.to_string()
            if self.child.op in (Op.ADD, Op.SUB):
                return f"(-{s})"
            return f"-{s}"
        if op in (Op.ADD, Op.SUB):
            l, r = self.left.to_string(), self.right.to_string()
            if self.right.op in (Op.ADD, Op.SUB, Op.NEG):
                r = f"({r})"
            return f"{l} {op.name[0].lower()} {r}" if op == Op.ADD else f"{l} - {r}"
        if op in (Op.MUL, Op.DIV):
            l, r = self.left.to_string(), self.right.to_string()
            if self.left.op in (Op.ADD, Op.SUB, Op.NEG):
                l = f"({l})"
            if self.right.op in (Op.ADD, Op.SUB, Op.NEG, Op.MUL, Op.DIV):
                r = f"({r})"
            return f"{l}·{r}" if op == Op.MUL else f"{l}/{r}"
        if op == Op.POW:
            l, r = self.left.to_string(), self.right.to_string()
            if self.left.op in (Op.ADD, Op.SUB, Op.NEG, Op.MUL, Op.DIV):
                l = f"({l})"
            if self.right.op in (Op.ADD, Op.SUB, Op.NEG, Op.MUL, Op.DIV, Op.POW):
                r = f"({r})"
            return f"{l}^{r}"
        # 函数
        arg = self.child.to_string()
        if self.child.op in (Op.ADD, Op.SUB):
            arg = f"({arg})"
        names = {
            Op.SIN: "sin", Op.COS: "cos", Op.TAN: "tan",
            Op.EXP: "exp", Op.LOG: "ln", Op.SQRT: "√",
            Op.ABS: "|·|", Op.ARCSIN: "arcsin", Op.ARCCOS: "arccos",
            Op.ARCTAN: "arctan", Op.SINH: "sinh", Op.COSH: "cosh",
            Op.TANH: "tanh", Op.SEC: "sec", Op.CSC: "csc",
            Op.COT: "cot", Op.FLOOR: "⌊·⌋", Op.CEIL: "⌈·⌉",
            Op.SIGN: "sgn",
        }
        return f"{names.get(op, op.name)}({arg})"

    # ---- 结构查询 ----
    def all_ops(self) -> set[Op]:
        """返回表达式中出现的所有操作符。"""
        ops = {self.op}
        for c in (self.child, self.left, self.right):
            if c:
                ops |= c.all_ops()
        return ops

    def node_count(self) -> int:
        """AST 节点总数（用于优雅度评分）。"""
        n = 1
        for c in (self.child, self.left, self.right):
            if c:
                n += c.node_count()
        return n

    def max_depth(self) -> int:
        d = 0
        for c in (self.child, self.left, self.right):
            if c:
                d = max(d, c.max_depth())
        return d + 1

    def function_calls(self) -> int:
        """函数调用次数（不含 +,-,*,/,^）。"""
        n = 0
        if self.op not in (Op.CONST, Op.VAR, Op.PI, Op.E,
                           Op.NEG, Op.ADD, Op.SUB, Op.MUL, Op.DIV, Op.POW):
            n += 1
        for c in (self.child, self.left, self.right):
            if c:
                n += c.function_calls()
        return n

    def has_non_analytic(self) -> bool:
        """是否包含非解析函数。"""
        return bool(self.all_ops() & NON_ANALYTIC_OPS)

    def variables(self) -> set[str]:
        vs = set()
        if self.op == Op.VAR:
            vs.add(self.var)
        for c in (self.child, self.left, self.right):
            if c:
                vs |= c.variables()
        return vs


# ============================================================
# 2. 解析器（递归下降）
# ============================================================

class ParseError(Exception):
    pass


_TOKEN_RE = re.compile(r"""
    (?P<NUMBER>\d+\.?\d*|\.\d+)
  | (?P<IDENT>[a-zA-Z_][a-zA-Z_0-9]*)
  | (?P<OP>[+\-*/^(),])
  | (?P<PI>π)
  | (?P<SPACE>\s+)
""", re.VERBOSE)

FUNC_NAMES = {
    "sin": Op.SIN, "cos": Op.COS, "tan": Op.TAN,
    "exp": Op.EXP, "log": Op.LOG, "ln": Op.LOG, "sqrt": Op.SQRT,
    "abs": Op.ABS, "arcsin": Op.ARCSIN, "arccos": Op.ARCCOS,
    "arctan": Op.ARCTAN, "sinh": Op.SINH, "cosh": Op.COSH,
    "tanh": Op.TANH, "sec": Op.SEC, "csc": Op.CSC, "cot": Op.COT,
    "floor": Op.FLOOR, "ceil": Op.CEIL, "sign": Op.SIGN,
    "sgn": Op.SIGN,
}

CONST_NAMES = {"pi": Op.PI, "PI": Op.PI, "e": Op.E, "E": Op.E}


class Parser:
    """
    解析数学表达式字符串为 Expr 树。
    支持: 数字, 变量, 函数, +, -, *, /, ^, 括号, π, e
    隐式乘法: 2x → 2*x, x sin(x) → x*sin(x)
    """

    def __init__(self, text: str):
        self.text = text
        self.tokens: list[tuple[str, str]] = []
        self.pos = 0
        self._tokenize()

    def _tokenize(self):
        pos = 0
        while pos < len(self.text):
            m = _TOKEN_RE.match(self.text, pos)
            if not m:
                raise ParseError(f"无法识别的字符 '{self.text[pos]}' 在位置 {pos}")
            kind = m.lastgroup
            val = m.group()
            if kind != "SPACE":
                self.tokens.append((kind, val))
            pos = m.end()

    def _peek(self) -> Optional[tuple[str, str]]:
        if self.pos < len(self.tokens):
            return self.tokens[self.pos]
        return None

    def _eat(self, kind: Optional[str] = None) -> tuple[str, str]:
        tok = self._peek()
        if tok is None:
            raise ParseError("表达式意外结束")
        if kind and tok[0] != kind:
            raise ParseError(f"期望 {kind}，得到 {tok[1]}")
        self.pos += 1
        return tok

    def parse(self) -> Expr:
        e = self._expr()
        if self.pos < len(self.tokens):
            raise ParseError(f"表达式末尾有多余内容: {self.tokens[self.pos][1]}")
        return e

    def _expr(self) -> Expr:
        """expr := term (('+' | '-') term)*"""
        e = self._term()
        while True:
            tok = self._peek()
            if tok and tok[0] == "OP" and tok[1] == "+":
                self._eat()
                e = Expr.add(e, self._term())
            elif tok and tok[0] == "OP" and tok[1] == "-":
                self._eat()
                e = Expr.sub(e, self._term())
            else:
                break
        return e

    def _term(self) -> Expr:
        """term := factor (('*' | '/') factor | 隐式乘法 factor)*"""
        e = self._factor()
        while True:
            tok = self._peek()
            if tok and tok[0] == "OP" and tok[1] == "*":
                self._eat()
                e = Expr.mul(e, self._factor())
            elif tok and tok[0] == "OP" and tok[1] == "/":
                self._eat()
                e = Expr.div(e, self._factor())
            elif tok and tok[0] in ("NUMBER", "IDENT", "PI"):
                # 隐式乘法: 2x, x sin(x), 2 sin(x)
                e = Expr.mul(e, self._factor())
            else:
                break
        return e

    def _factor(self) -> Expr:
        """factor := ('+' | '-') factor | power"""
        tok = self._peek()
        if tok and tok[0] == "OP" and tok[1] == "+":
            self._eat()
            return self._factor()
        if tok and tok[0] == "OP" and tok[1] == "-":
            self._eat()
            return Expr.neg(self._factor())
        return self._power()

    def _power(self) -> Expr:
        """power := atom ('^' factor)?  (右结合)"""
        e = self._atom()
        tok = self._peek()
        if tok and tok[0] == "OP" and tok[1] == "^":
            self._eat()
            e = Expr.pow(e, self._factor())
        return e

    def _atom(self) -> Expr:
        """atom := NUMBER | IDENT | π | e | '(' expr ')'"""
        tok = self._peek()
        if tok is None:
            raise ParseError("期望表达式")

        kind, val = tok

        if kind == "NUMBER":
            self._eat()
            return Expr.const(float(val))

        if kind == "PI":
            self._eat()
            return Expr(Op.PI, value=math.pi)

        if kind == "IDENT":
            self._eat()
            # 函数?
            if self._peek() and self._peek()[0] == "OP" and self._peek()[1] == "(":
                if val in FUNC_NAMES:
                    self._eat()  # (
                    arg = self._expr()
                    self._eat("OP")  # )
                    return Expr.func(FUNC_NAMES[val], arg)
                else:
                    raise ParseError(f"未知函数 '{val}'")
            # 常量?
            if val in CONST_NAMES:
                if val in ("pi", "PI"):
                    return Expr(Op.PI, value=math.pi)
                return Expr(Op.E, value=math.e)
            # 变量
            return Expr.var(val)

        if kind == "OP" and val == "(":
            self._eat()
            e = self._expr()
            self._eat("OP")  # )
            return e

        raise ParseError(f"意外的 token '{val}'")


def parse_expr(text: str) -> Expr:
    """解析表达式字符串。"""
    return Parser(text).parse()


# ============================================================
# 3. 符号求导
# ============================================================

def differentiate(e: Expr, var: str) -> Expr:
    """对 e 关于 var 求导。"""
    op = e.op

    if op == Op.CONST or op == Op.PI or op == Op.E:
        return Expr.const(0.0)

    if op == Op.VAR:
        return Expr.const(1.0) if e.var == var else Expr.const(0.0)

    if op == Op.NEG:
        return Expr.neg(differentiate(e.child, var))

    if op == Op.ADD:
        return Expr.add(differentiate(e.left, var), differentiate(e.right, var))

    if op == Op.SUB:
        return Expr.sub(differentiate(e.left, var), differentiate(e.right, var))

    if op == Op.MUL:
        # (uv)' = u'v + uv'
        return Expr.add(
            Expr.mul(differentiate(e.left, var), e.right),
            Expr.mul(e.left, differentiate(e.right, var)),
        )

    if op == Op.DIV:
        # (u/v)' = (u'v - uv') / v²
        v2 = Expr.pow(e.right, Expr.const(2.0))
        num = Expr.sub(
            Expr.mul(differentiate(e.left, var), e.right),
            Expr.mul(e.left, differentiate(e.right, var)),
        )
        return Expr.div(num, v2)

    if op == Op.POW:
        # 情况1: 底数含变量，指数为常量 → n·x^(n-1)·x'
        # 情况2: 底数为常量，指数含变量 → a^u · ln(a) · u'
        # 情况3: 都含变量 → x^u · (u'·ln(x) + u·x'/x)
        left_has_var = var in e.left.variables()
        right_has_var = var in e.right.variables()

        if left_has_var and not right_has_var:
            # n·x^(n-1)
            return Expr.mul(
                e.right,
                Expr.mul(
                    Expr.pow(e.left, Expr.sub(e.right, Expr.const(1.0))),
                    differentiate(e.left, var),
                ),
            )
        elif not left_has_var and right_has_var:
            # a^u · ln(a) · u'
            return Expr.mul(
                Expr.mul(e, Expr.log(e.left)),
                differentiate(e.right, var),
            )
        else:
            # 一般情况
            return Expr.mul(
                e,
                Expr.add(
                    Expr.mul(differentiate(e.right, var), Expr.log(e.left)),
                    Expr.mul(e.right, Expr.div(differentiate(e.left, var), e.left)),
                ),
            )

    # 函数求导（链式法则）
    inner = e.child
    inner_d = differentiate(inner, var)

    deriv_map = {
        Op.SIN: lambda: Expr.mul(Expr.cos(inner), inner_d),
        Op.COS: lambda: Expr.neg(Expr.mul(Expr.sin(inner), inner_d)),
        Op.TAN: lambda: Expr.div(inner_d, Expr.pow(Expr.cos(inner), Expr.const(2.0))),
        Op.EXP: lambda: Expr.mul(inner, inner_d),  # exp(u)' = exp(u)·u'
        Op.LOG: lambda: Expr.div(inner_d, inner),
        Op.SQRT: lambda: Expr.div(inner_d, Expr.mul(Expr.const(2.0), Expr.sqrt(inner))),
        Op.ABS: lambda: Expr.mul(Expr.sign(inner), inner_d),
        Op.ARCSIN: lambda: Expr.div(inner_d, Expr.sqrt(Expr.sub(Expr.const(1.0), Expr.pow(inner, Expr.const(2.0))))),
        Op.ARCCOS: lambda: Expr.neg(Expr.div(inner_d, Expr.sqrt(Expr.sub(Expr.const(1.0), Expr.pow(inner, Expr.const(2.0)))))),
        Op.ARCTAN: lambda: Expr.div(inner_d, Expr.add(Expr.const(1.0), Expr.pow(inner, Expr.const(2.0)))),
        Op.SINH: lambda: Expr.mul(Expr.cosh(inner), inner_d),
        Op.COSH: lambda: Expr.mul(Expr.sinh(inner), inner_d),
        Op.TANH: lambda: Expr.div(inner_d, Expr.pow(Expr.cosh(inner), Expr.const(2.0))),
        Op.SEC: lambda: Expr.mul(Expr.mul(Expr.sec(inner), Expr.tan(inner)), inner_d),
        Op.CSC: lambda: Expr.neg(Expr.mul(Expr.mul(Expr.csc(inner), Expr.cot(inner)), inner_d)),
        Op.COT: lambda: Expr.neg(Expr.div(inner_d, Expr.pow(Expr.sin(inner), Expr.const(2.0)))),
        Op.FLOOR: lambda: Expr.const(0.0),  # 几乎处处为 0
        Op.CEIL: lambda: Expr.const(0.0),
        Op.SIGN: lambda: Expr.const(0.0),  # 几乎处处为 0
    }

    if op in deriv_map:
        return deriv_map[op]()

    raise ValueError(f"无法对 {op} 求导")


def nth_derivative(e: Expr, var: str, n: int) -> Expr:
    """n 阶导数。"""
    result = e
    for _ in range(n):
        result = differentiate(result, var)
    return result


# ============================================================
# 4. 代入与数值求值
# ============================================================

def substitute(e: Expr, var: str, value: Expr) -> Expr:
    """符号代入：将 e 中的 var 替换为 value。"""
    if e.op == Op.VAR and e.var == var:
        return value
    if e.op in (Op.CONST, Op.PI, Op.E):
        return e
    if e.op == Op.NEG:
        return Expr.neg(substitute(e.child, var, value))
    if e.op in (Op.ADD, Op.SUB, Op.MUL, Op.DIV, Op.POW):
        return Expr(e.op,
                    left=substitute(e.left, var, value),
                    right=substitute(e.right, var, value))
    # 函数
    return Expr(e.op, child=substitute(e.child, var, value))


def evaluate(e: Expr, bindings: dict[str, float]) -> float:
    """数值求值。"""
    op = e.op
    if op == Op.CONST:
        return e.value
    if op == Op.PI:
        return math.pi
    if op == Op.E:
        return math.e
    if op == Op.VAR:
        if e.var not in bindings:
            raise ValueError(f"未绑定的变量 '{e.var}'")
        return bindings[e.var]
    if op == Op.NEG:
        return -evaluate(e.child, bindings)
    if op == Op.ADD:
        return evaluate(e.left, bindings) + evaluate(e.right, bindings)
    if op == Op.SUB:
        return evaluate(e.left, bindings) - evaluate(e.right, bindings)
    if op == Op.MUL:
        return evaluate(e.left, bindings) * evaluate(e.right, bindings)
    if op == Op.DIV:
        d = evaluate(e.right, bindings)
        if d == 0:
            raise ZeroDivisionError("除以零")
        return evaluate(e.left, bindings) / d
    if op == Op.POW:
        return evaluate(e.left, bindings) ** evaluate(e.right, bindings)

    v = evaluate(e.child, bindings)
    func_map = {
        Op.SIN: math.sin, Op.COS: math.cos, Op.TAN: math.tan,
        Op.EXP: math.exp, Op.LOG: math.log, Op.SQRT: math.sqrt,
        Op.ABS: abs, Op.ARCSIN: math.asin, Op.ARCCOS: math.acos,
        Op.ARCTAN: math.atan, Op.SINH: math.sinh, Op.COSH: math.cosh,
        Op.TANH: math.tanh,
    }
    if op in func_map:
        return func_map[op](v)
    if op == Op.SEC:
        return 1.0 / math.cos(v)
    if op == Op.CSC:
        return 1.0 / math.sin(v)
    if op == Op.COT:
        return 1.0 / math.tan(v)
    if op == Op.FLOOR:
        return math.floor(v)
    if op == Op.CEIL:
        return math.ceil(v)
    if op == Op.SIGN:
        return (v > 0) - (v < 0)

    raise ValueError(f"无法求值 {op}")


# ============================================================
# 5. 化简
# ============================================================

def simplify(e: Expr) -> Expr:
    """
    基础化简：
    - 常量折叠
    - x + 0 = x, x * 1 = x, x * 0 = 0
    - x - x = 0
    - sin²(x) + cos²(x) = 1（模式匹配）
    - 合并同类项（x + x = 2x）
    """
    e = _simplify_children(e)
    op = e.op

    # 常量折叠
    if op == Op.CONST:
        return e

    if op in (Op.ADD, Op.SUB, Op.MUL, Op.DIV, Op.POW):
        l = e.left
        r = e.right
        if op == Op.ADD:
            if l.op == Op.CONST and l.value == 0:
                return r
            if r.op == Op.CONST and r.value == 0:
                return l
            if l.op == Op.CONST and r.op == Op.CONST:
                return Expr.const(l.value + r.value)
        elif op == Op.SUB:
            if r.op == Op.CONST and r.value == 0:
                return l
            if l.op == Op.CONST and r.op == Op.CONST:
                return Expr.const(l.value - r.value)
            # x - x = 0
            if _structurally_equal(l, r):
                return Expr.const(0.0)
        elif op == Op.MUL:
            if l.op == Op.CONST and l.value == 1:
                return r
            if r.op == Op.CONST and r.value == 1:
                return l
            if l.op == Op.CONST and l.value == 0:
                return Expr.const(0.0)
            if r.op == Op.CONST and r.value == 0:
                return Expr.const(0.0)
            if l.op == Op.CONST and r.op == Op.CONST:
                return Expr.const(l.value * r.value)
        elif op == Op.DIV:
            if r.op == Op.CONST and r.value == 1:
                return l
            if l.op == Op.CONST and r.op == Op.CONST:
                if r.value == 0:
                    raise ZeroDivisionError("除以零")
                return Expr.const(l.value / r.value)
        elif op == Op.POW:
            if r.op == Op.CONST and r.value == 0:
                return Expr.const(1.0)
            if r.op == Op.CONST and r.value == 1:
                return l
            if l.op == Op.CONST and r.op == Op.CONST:
                return Expr.const(l.value ** r.value)

    # 三角恒等式: sin²(x) + cos²(x) → 1
    if op == Op.ADD:
        result = _match_trig_identity(e)
        if result is not None:
            return result

    # 双重否定: --x → x
    if op == Op.NEG and e.child.op == Op.NEG:
        return e.child.child

    return e


def _simplify_children(e: Expr) -> Expr:
    """递归化简子节点。"""
    if e.op == Op.NEG:
        return Expr.neg(simplify(e.child))
    if e.op in (Op.ADD, Op.SUB, Op.MUL, Op.DIV, Op.POW):
        return Expr(e.op, left=simplify(e.left), right=simplify(e.right))
    if e.op not in (Op.CONST, Op.VAR, Op.PI, Op.E):
        return Expr(e.op, child=simplify(e.child))
    return e


def _match_trig_identity(e: Expr) -> Optional[Expr]:
    """匹配 sin²(x) + cos²(x) = 1。"""
    if e.op != Op.ADD:
        return None
    a, b = e.left, e.right
    for first, second in ((a, b), (b, a)):
        if (first.op == Op.POW and second.op == Op.POW
                and first.left.op == Op.SIN and second.left.op == Op.COS
                and first.right.op == Op.CONST and first.right.value == 2
                and second.right.op == Op.CONST and second.right.value == 2
                and _structurally_equal(first.left.child, second.left.child)):
            return Expr.const(1.0)
    return None


def _structurally_equal(a: Expr, b: Expr) -> bool:
    """结构相等检查（不做化简）。"""
    if a.op != b.op:
        return False
    if a.op == Op.CONST:
        return a.value == b.value
    if a.op == Op.VAR:
        return a.var == b.var
    if a.op == Op.NEG:
        return _structurally_equal(a.child, b.child)
    if a.op in (Op.ADD, Op.SUB, Op.MUL, Op.DIV, Op.POW):
        return (_structurally_equal(a.left, b.left)
                and _structurally_equal(a.right, b.right))
    if a.op not in (Op.CONST, Op.VAR, Op.PI, Op.E):
        return _structurally_equal(a.child, b.child)
    return True


# ============================================================
# 6. 定义域分析
# ============================================================

def find_singularities(e: Expr, var: str, lo: float, hi: float,
                       samples: int = 1000) -> list[float]:
    """
    在 [lo, hi] 上数值检测奇点（函数值发散或 NaN 的点）。
    返回奇点位置列表。
    """
    singularities = []
    dx = (hi - lo) / samples
    prev_val = None
    for i in range(samples + 1):
        x = lo + i * dx
        try:
            v = evaluate(e, {var: x})
            if math.isnan(v) or math.isinf(v):
                singularities.append(x)
            elif prev_val is not None and abs(v - prev_val) > 1e6:
                # 跳变，可能是奇点
                singularities.append(x)
            prev_val = v
        except (ZeroDivisionError, ValueError, OverflowError):
            singularities.append(x)
    return singularities


def check_domain(e: Expr, var: str, lo: float, hi: float) -> tuple[bool, list[float]]:
    """
    检查 e 在 [lo, hi] 上是否有定义。
    返回 (是否有定义, 奇点列表)。
    """
    singularities = find_singularities(e, var, lo, hi)
    # 过滤掉端点附近的误报
    interior = [s for s in singularities if lo < s < hi]
    return (len(interior) == 0, interior)


# ============================================================
# 7. 约束验证
# ============================================================

@dataclass
class ConstraintResult:
    constraint: str
    passed: bool
    detail: str = ""


def check_boundary(e: Expr, var: str, x0: float, expected: float,
                   tol: float = 1e-9) -> ConstraintResult:
    """检查 f(x0) == expected。"""
    try:
        v = evaluate(e, {var: x0})
        passed = abs(v - expected) < tol
        return ConstraintResult(
            f"f({x0}) = {expected}",
            passed,
            f"f({x0}) = {v:.10f}" + ("" if passed else f" ≠ {expected}")
        )
    except Exception as ex:
        return ConstraintResult(f"f({x0}) = {expected}", False, f"求值失败: {ex}")


def check_monotonic(e: Expr, var: str, lo: float, hi: float,
                    direction: str = "increasing",
                    samples: int = 500) -> ConstraintResult:
    """
    检查单调性：符号求导 + 数值验证。
    direction: 'increasing' 或 'decreasing'
    """
    de = simplify(differentiate(e, var))
    dx = (hi - lo) / samples
    violations = 0
    for i in range(samples + 1):
        x = lo + i * dx
        try:
            dv = evaluate(de, {var: x})
            if direction == "increasing" and dv < -1e-9:
                violations += 1
            elif direction == "decreasing" and dv > 1e-9:
                violations += 1
        except Exception:
            violations += 1

    passed = violations == 0
    desc = f"f 在 [{lo}, {hi}] 上{'递增' if direction == 'increasing' else '递减'}"
    return ConstraintResult(
        desc, passed,
        f"导数违例 {violations}/{samples + 1} 点" if not passed else "导数符号一致"
    )


def check_parity(e: Expr, var: str, expected: str,
                 samples: int = 200) -> ConstraintResult:
    """
    检查奇偶性。
    expected: 'even' 或 'odd'
    方法：数值验证 f(-x) vs f(x)
    """
    lo, hi = -5.0, 5.0
    dx = (hi - lo) / samples
    violations = 0
    for i in range(samples + 1):
        x = lo + i * dx
        try:
            fx = evaluate(e, {var: x})
            fnx = evaluate(e, {var: -x})
            if expected == "even" and abs(fx - fnx) > 1e-6:
                violations += 1
            elif expected == "odd" and abs(fx + fnx) > 1e-6:
                violations += 1
        except Exception:
            violations += 1

    passed = violations == 0
    desc = f"f 是{'偶' if expected == 'even' else '奇'}函数"
    return ConstraintResult(
        desc, passed,
        f"违例 {violations}/{samples + 1} 点" if not passed else "奇偶性成立"
    )


def check_analytic(e: Expr, var: str, lo: float, hi: float) -> ConstraintResult:
    """
    检查解析性：
    1. 不含非解析函数 (abs, floor, ceil, sign)
    2. 在 (lo, hi) 内无奇点
    """
    if e.has_non_analytic():
        bad = e.all_ops() & NON_ANALYTIC_OPS
        return ConstraintResult(
            f"f 在 ({lo}, {hi}) 上解析",
            False,
            f"包含非解析函数: {', '.join(o.name for o in bad)}"
        )

    ok, singularities = check_domain(e, var, lo, hi)
    if not ok:
        return ConstraintResult(
            f"f 在 ({lo}, {hi}) 上解析",
            False,
            f"区域内存在奇点: {singularities[:5]}"
        )

    return ConstraintResult(
        f"f 在 ({lo}, {hi}) 上解析",
        True,
        "无非解析函数且区域内无奇点"
    )


def check_periodic(e: Expr, var: str, period: float,
                   samples: int = 200) -> ConstraintResult:
    """检查周期性：f(x + T) = f(x)。"""
    lo, hi = -10.0, 10.0
    dx = (hi - lo) / samples
    violations = 0
    for i in range(samples + 1):
        x = lo + i * dx
        try:
            fx = evaluate(e, {var: x})
            fxp = evaluate(e, {var: x + period})
            if abs(fx - fxp) > 1e-6:
                violations += 1
        except Exception:
            violations += 1

    passed = violations == 0
    return ConstraintResult(
        f"f 的周期为 {period}",
        passed,
        f"违例 {violations}/{samples + 1} 点" if not passed else "周期性成立"
    )


def check_allowed_functions(e: Expr, allowed: set[str]) -> ConstraintResult:
    """检查是否只使用了允许的函数。"""
    op_to_name = {
        Op.SIN: "sin", Op.COS: "cos", Op.TAN: "tan",
        Op.EXP: "exp", Op.LOG: "log", Op.SQRT: "sqrt",
        Op.ABS: "abs", Op.ARCSIN: "arcsin", Op.ARCCOS: "arccos",
        Op.ARCTAN: "arctan", Op.SINH: "sinh", Op.COSH: "cosh",
        Op.TANH: "tanh", Op.SEC: "sec", Op.CSC: "csc",
        Op.COT: "cot", Op.FLOOR: "floor", Op.CEIL: "ceil",
        Op.SIGN: "sign",
    }
    used = set()
    for op in e.all_ops():
        if op in op_to_name:
            used.add(op_to_name[op])

    disallowed = used - allowed
    passed = len(disallowed) == 0
    return ConstraintResult(
        f"只使用允许的函数 {sorted(allowed)}",
        passed,
        f"使用了未允许的函数: {sorted(disallowed)}" if not passed else "函数使用合规"
    )


# ============================================================
# 8. 评分
# ============================================================

@dataclass
class ScoreResult:
    correctness: bool
    accuracy: float       # 0-100
    elegance: float       # 0-100
    robustness: float     # 0-100
    time_bonus: float     # 0-1 (仅限时模式)
    total: float          # 0-100
    grade: str            # S/A/B/C/D
    details: list[ConstraintResult] = field(default_factory=list)

    def summary(self) -> str:
        lines = [
            f"评级: {self.grade}  总分: {self.total:.1f}",
            f"  正确性: {'✓' if self.correctness else '✗'}",
            f"  精确度: {self.accuracy:.1f}",
            f"  优雅度: {self.elegance:.1f}",
            f"  鲁棒性: {self.robustness:.1f}",
        ]
        if self.time_bonus > 0:
            lines.append(f"  时间加成: +{self.time_bonus * 100:.0f}%")
        lines.append("")
        for d in self.details:
            mark = "✓" if d.passed else "✗"
            lines.append(f"  {mark} {d.constraint}: {d.detail}")
        return "\n".join(lines)


def score_submission(
    e: Expr,
    var: str,
    constraints: list[ConstraintResult],
    time_remaining: Optional[float] = None,
    time_limit: Optional[float] = None,
) -> ScoreResult:
    """
    综合评分。
    constraints: 各约束的验证结果列表
    time_remaining / time_limit: 限时模式参数
    """
    # 正确性
    all_passed = all(c.passed for c in constraints)

    # 精确度：所有约束通过 → 100，否则按通过比例
    if constraints:
        passed_count = sum(1 for c in constraints if c.passed)
        accuracy = (passed_count / len(constraints)) * 100
    else:
        accuracy = 100.0

    # 优雅度：基于 AST 复杂度
    nc = e.node_count()
    fc = e.function_calls()
    depth = e.max_depth()
    # 基准：10 节点、2 次函数调用、深度 3 → 100 分
    elegance = max(0, 100 - max(0, nc - 10) * 3 - max(0, fc - 2) * 5 - max(0, depth - 3) * 5)

    # 鲁棒性：简化（初期 = 精确度，后期可加入边界附近检查）
    robustness = accuracy

    # 时间加成
    time_bonus = 0.0
    if time_remaining is not None and time_limit is not None and time_limit > 0:
        time_bonus = min(1.0, max(0.0, time_remaining / time_limit)) * 0.2  # 最多 +20%

    # 总分
    if not all_passed:
        total = 0.0
    else:
        base = (accuracy * 0.4 + elegance * 0.35 + robustness * 0.25)
        total = base * (1 + time_bonus)
        total = min(100.0, total)

    # 评级
    if total >= 95:
        grade = "S"
    elif total >= 85:
        grade = "A"
    elif total >= 70:
        grade = "B"
    elif total >= 50:
        grade = "C"
    else:
        grade = "D"

    return ScoreResult(
        correctness=all_passed,
        accuracy=accuracy,
        elegance=elegance,
        robustness=robustness,
        time_bonus=time_bonus,
        total=total,
        grade=grade,
        details=constraints,
    )


# ============================================================
# 9. 便捷接口
# ============================================================

def verify_expression(
    text: str,
    var: str = "x",
    constraints: Optional[list[dict]] = None,
    allowed_functions: Optional[set[str]] = None,
    time_remaining: Optional[float] = None,
    time_limit: Optional[float] = None,
) -> tuple[Expr, list[ConstraintResult], ScoreResult]:
    """
    一站式验证接口。
    constraints: 约束列表，每项为 dict，type 字段指定类型：
      - {"type": "boundary", "x": 0, "value": 1}
      - {"type": "monotonic", "lo": 0, "hi": 3.14, "direction": "increasing"}
      - {"type": "parity", "expected": "even"}
      - {"type": "analytic", "lo": -1, "hi": 1}
      - {"type": "periodic", "period": 6.28}
    """
    e = parse_expr(text)
    results: list[ConstraintResult] = []

    if allowed_functions:
        results.append(check_allowed_functions(e, allowed_functions))

    for c in (constraints or []):
        ctype = c["type"]
        if ctype == "boundary":
            results.append(check_boundary(e, var, c["x"], c["value"]))
        elif ctype == "monotonic":
            results.append(check_monotonic(e, var, c["lo"], c["hi"], c.get("direction", "increasing")))
        elif ctype == "parity":
            results.append(check_parity(e, var, c["expected"]))
        elif ctype == "analytic":
            results.append(check_analytic(e, var, c["lo"], c["hi"]))
        elif ctype == "periodic":
            results.append(check_periodic(e, var, c["period"]))
        else:
            results.append(ConstraintResult(f"未知约束类型: {ctype}", False, "不支持的约束"))

    score = score_submission(e, var, results, time_remaining, time_limit)
    return e, results, score
