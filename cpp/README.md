# 函数工坊 FunctionCraft

> 在约束之中，构造属于你的函数。

一个关于函数的解谜游戏：给出若干限定条件（函数库、定义域、解析性、单调性、渐近线、零点数…），
玩家用计算器式键盘写一个函数，符号引擎严格验证并给出多维度评分。

```
关卡 1-1「余弦的微笑」
  约束：① 偶函数   ② f(0) = 1
  你的答案：cos(x)          → S  100 分
  别人的答案：x^2*0+cos(x)  → A   84.6 分（能做对，但不够优雅）
```

---

## 直接运行

双击 `FunctionCraft.exe`（在仓库根目录的 `FunctionCraft/` 下，或 `cpp/build/bin/`）。

---

## 玩法

| 模式 | 说明 |
|------|------|
| 关卡模式 | 24 关 / 4 章，约束从 1–2 条递进到 6 条 |
| 无尽模式 | 程序化生成，每 10 关一个 Boss 关，记录最高关数 |
| 限时模式 | 可叠加在任意模式上，每关 3 分钟，剩余时间转化为分数 |

**输入**：计算器键盘（鼠标点）或直接键盘输入，支持隐式乘法（`2x`、`3sin(x)`）。

**约束类型（15 种）**：函数库白名单、定义域、解析性、边界值、值域、恒正、单调性、奇偶性、
周期性、水平渐近线、极值、对称轴、零点个数、复杂度上限、中心对称。

**评分（多维度）**

```
总分 = 100 × (0.35·精确度 + 0.30·优雅度 + 0.25·鲁棒性 + 0.10·时间)
评级：S ≥ 90 / A ≥ 80 / B ≥ 70 / C ≥ 60 / 其余 D
```

- 精确度：边界值、值域类约束的数值误差
- 优雅度：表达式复杂度（函数调用数 / 节点数 / 嵌套深度）与参考解比较，`×0` 填充会被扣分
- 鲁棒性：在约束区间端点内缩/外扩处是否依然成立
- 时间：限时模式的剩余时间

---

## 目录结构

```
cpp/
├── CMakeLists.txt
├── build.bat            # 一键配置 + 编译（MSVC + Ninja）
├── test.bat             # 编译 + 跑全部单元测试
├── src/
│   ├── core/            # 符号引擎（无第三方依赖，可独立复用）
│   │   ├── Expr.h/.cpp       表达式树：不可变节点、优先级感知打印、哈希
│   │   ├── Parser.h/.cpp     词法 + 递归下降解析（隐式乘法、Unicode π/√/×/÷）
│   │   ├── Numeric.h/.cpp    复数求值
│   │   ├── Simplify.h/.cpp   域安全化简、符号等价判定（含数值兜底）
│   │   ├── Diff.h/.cpp       符号求导（全算子 + 任意阶）
│   │   ├── Domain.h/.cpp     定义域、解析性、单调性、值域、零点
│   │   ├── Limit.h/.cpp      极限与渐近线（Aitken 外推）
│   │   ├── Constraint.h/.cpp 15 种约束的验证器
│   │   ├── Scoring.h/.cpp    多维度评分
│   │   ├── Level.h/.cpp      关卡 JSON 加载
│   │   ├── EndlessGen.h/.cpp 无尽模式生成（先造解、再反推约束，保证可解）
│   │   └── Json.h/.cpp       极简 JSON
│   ├── ui/              # Dear ImGui + DirectX11 界面
│   └── tools/cli.cpp    # 命令行自检工具
├── data/levels.json     # 24 关关卡数据
├── tests/               # 172 个单元/集成测试（自带极简框架）
└── third_party/         # Dear ImGui、stb_image_write
```

---

## 构建

**环境**：Windows 10/11、Visual Studio 2022（MSVC 14.4+）、CMake 3.20+、Ninja。

```bat
cd cpp
build.bat        :: 配置 + 编译
test.bat         :: 编译 + 跑测试
build\bin\FunctionCraft.exe
```

`build.bat` 内置了本机工具链路径（VS 在 `D:\VS`，CMake/Ninja 在 STM32CubeCLT 目录）。
换机器时改 `build.bat` 顶部的 `PATH` 与 `vcvarsall.bat` 路径即可。

---

## 调试与自检

```bat
:: 命令行工具
build\fcg_cli.exe verify data\levels.json              :: 用每关参考解验证约束自洽
build\fcg_cli.exe check  data\levels.json 1-1 "cos(x)" :: 用任意表达式检查某关
build\fcg_cli.exe endless 12345 20                     :: 生成 20 关无尽关卡并自检
build\fcg_cli.exe bench  data\levels.json 4-6 "exp(-x*x)" 20  :: 评分耗时基准
build\fcg_cli.exe parse  "1/(1+x*x)"                   :: 打印 AST / 化简结果

:: 界面截图（渲染 N 帧后存 PNG，用于自动验收）
build\bin\FunctionCraft.exe --shot=shots\menu.png --frames=20 --scene=menu
build\bin\FunctionCraft.exe --shot=shots\play.png --scene=play --level=1-1 --expr="cos(x)"

:: 单元测试（可带子串过滤）
build\fcg_tests.exe
build\fcg_tests.exe gameplay
```

---

## 引擎设计要点

1. **AST 与数值类型无关**：`Op` 枚举统一描述实数/复数/多变量运算，为复变函数、多元函数扩展留好接口。
2. **符号优先**：解析性、单调性、奇偶性、周期性用符号判定；符号化简失败时才回退到数值采样兜底。
3. **域安全化简**：化简不允许改变定义域（`x/x` 不会化为 `1`，`sqrt(x²)` 只能化为 `|x|`），否则约束验证会出现假阳性。
4. **关卡可解性保证**：无尽模式先生成参考解，再由参考解反推约束（采样判定奇偶性/单调性/值域/零点/渐近线），确保每关必然有解。
5. **性能**：单次提交评分 0.01–3.7 ms；符号化简典型耗时 0.065 ms。

---

## 测试

```
172 个测试全绿：AST/解析/求值（8）+ 化简与求导（40）+ 定义域/极限（77）+ 玩法系统（35）+ 跨模块集成（12）
```

集成测试覆盖：24 关参考解全部满足约束、常见正确解被接受、常见错误解被拒绝、
打印→解析→符号等价回环、评分单调性、失败归零、限时加成、无尽模式 180 关可解性、提交性能。

---

## 扩展路线

- **复数域**：柯西-黎曼方程判定解析性、留数、洛朗展开（`Cplx` 已就绪）
- **多变量**：偏导数、Hessian 极值判定、向量场
- **离散 / 泛函**：递推关系、正交函数系

---

## 已知限制

- 周期型定义域（如 `tan`）的奇点在扫描窗口内精确表示，窗口外以 ±∞ 区间近似
- 极慢收敛的极限（如 `1/ln(x) → 0`）可能返回 `Unknown`
- 严格不等式且排除点为零测集的复合条件，数值扫描可能漏掉孤立排除点
