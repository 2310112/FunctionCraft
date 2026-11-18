# 函数工坊 FunctionCraft

一个关于函数的解谜游戏：在限定条件下构造出满足所有约束的函数。

```
约束：定义域 [0, 2π] 上单调递增，f(0)=0，f(2π)=1，且是周期函数
你的答案：x/(2π) + sin(x)/(2π) ...
```

符号引擎会严格验证每一条约束（不是数值采样近似），并从**精确度 / 优雅度 / 鲁棒性 / 时间**四个维度评分。

---

## 玩法

| 模式 | 说明 |
|------|------|
| 关卡模式 | 4 章递进关卡，从单约束到解析性/渐近线/复杂度上限 |
| 无尽模式 | 程序化生成，每 10 关一个 Boss 关 |
| 限时模式 | 可叠加在任意模式上，剩余时间转化为分数加成 |

**输入方式**：类计算器键盘（函数键 + 数字 + 运算符），也支持直接键盘输入。

**约束类型**：函数库白名单、定义域、解析性、边界值、值域、单调性、奇偶性、周期性、水平渐近线、极值、对称轴、零点个数、复杂度上限。

---

## 目录结构

```
cpp/
├── CMakeLists.txt
├── build.bat            # 一键配置 + 编译
├── test.bat             # 编译 + 运行全部单元测试
├── src/
│   ├── core/            # 符号引擎（无外部依赖，可独立复用）
│   │   ├── Expr.h/.cpp          表达式树：不可变节点、打印、哈希
│   │   ├── Parser.h/.cpp        词法分析 + 递归下降解析（支持隐式乘法、Unicode 符号）
│   │   ├── Numeric.h/.cpp       复数求值
│   │   ├── Simplify.h/.cpp      符号化简、符号等价判定
│   │   ├── Diff.h/.cpp          符号求导（任意阶）
│   │   ├── Domain.h/.cpp        定义域、解析性、单调性、值域、零点
│   │   ├── Limit.h/.cpp         极限与渐近线
│   │   ├── Constraint.h/.cpp    约束模型与验证器
│   │   ├── Scoring.h/.cpp       多维评分
│   │   ├── Level.h/.cpp         关卡 JSON 加载
│   │   ├── EndlessGen.h/.cpp    无尽模式关卡生成
│   │   └── Json.h/.cpp          极简 JSON
│   └── ui/              # Dear ImGui + DirectX11 界面
├── data/levels.json     # 关卡数据
├── tests/               # 单元测试（自带极简框架）
└── third_party/         # Dear ImGui、stb_image_write
```

---

## 构建

**环境要求**：Windows 10/11、Visual Studio 2022（MSVC 14.4+）、CMake 3.20+、Ninja。

```bat
cd cpp
build.bat           :: 配置 + 编译
test.bat            :: 编译 + 跑单元测试
build\bin\FunctionCraft.exe
```

`build.bat` 已内置工具链路径（VS 在 `D:\VS`，CMake/Ninja 在 STM32CubeCLT 目录）。
换机器时改 `build.bat` 顶部的 `PATH` 与 `vcvarsall.bat` 路径即可。

---

## 调试与验收

```bat
:: 渲染 N 帧后截图（用于自动验收界面）
build\bin\FunctionCraft.exe --shot=shots\menu.png --frames=20
build\bin\FunctionCraft.exe --shot=shots\play.png --scene=play --level=1-1 --expr="cos(x)"

:: 单元测试（可带子串过滤）
build\fcg_tests.exe
build\fcg_tests.exe simplify
```

---

## 引擎设计要点

1. **AST 与数值类型无关**：`Op` 枚举统一描述实数/复数/多变量运算，为后续复变函数、多元函数扩展留好接口。
2. **符号优先**：解析性、单调性、奇偶性、周期性都用符号判定；只有在符号无法化简时才回退到数值采样兜底。
3. **域安全化简**：化简不允许改变定义域（例如 `x/x` 不会化为 `1`），否则约束验证会出现假阳性。
4. **关卡可解性保证**：无尽模式先生成参考解，再由参考解反推约束，确保每关必然有解。

---

## 扩展路线

- **复数域**：柯西-黎曼方程判定解析性、留数、洛朗展开
- **多变量**：偏导数、Hessian 极值判定、向量场
- **离散/泛函**：递推关系、正交函数系

`Op` 枚举与 `Cplx` 数值类型已为这些扩展预留。
