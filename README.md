# 棋析 · XiangqiAnalyzer

一款基于 **C++17 / Qt 6** 的本地中国象棋局势分析工具，支持自由摆棋、局势评分、推荐着法、变化预览、棋盘图片识别和自动对弈。

本项目面向 **macOS**。分析、图片识别和自动对弈均在本地运行。

## 功能特性

- **自由摆棋**：使用真实棋子图标摆放、删除和旋转局面，支持空棋盘、初始布局与 FEN 导入。
- **本地局势分析**：显示红方视角评分、推荐着法、搜索深度和主要变化。
- **变化逐步预览**：点击中文着法，在小窗中前后浏览、拖动定位，并将当前步骤应用到主棋盘。
- **电脑走棋**：根据当前局面计算并执行推荐着法。
- **棋盘图片识别**：支持系统截图、导入图片和直接拖入图片，识别后可人工校对并摆到主棋盘。
- **红方在上或在下**：支持己方执红、执黑以及红方位于棋盘上方时的识别和自动点击。
- **自动对弈**：支持天天象棋、JJ 象棋及其他象棋应用窗口；连续识别并校验局面后自动走棋。
- **内置识别模型**：仓库包含全部棋盘定位、棋子检测和整盘分类 ONNX 模型。
- **可配置分析组件**：默认使用指定目录中的 Pikafish，也可在“分析设置”中选择其他本地可执行文件。

## 使用限制与公平竞技

本项目仅供个人学习、复盘研究、离线分析和经授权的测试使用。**禁止将本项目用于任何线上对弈中的实时辅助、代走或其他作弊行为**，包括违反天天象棋、JJ 象棋或其他平台规则的辅助对弈。使用者应遵守所在平台的用户协议与公平竞技要求，并自行承担违规使用产生的后果。

## 运行环境

- macOS 14 或更高版本
- Apple Silicon Mac（ARM64）
- 自动窗口截图和点击需要 macOS 的“屏幕录制与系统录音”及“辅助功能”权限

## 仓库已包含的识别模型

| 文件 | 用途 |
| --- | --- |
| `third_party/models/theone1006/pose.onnx` | 定位棋盘四角并进行透视校正 |
| `third_party/models/theone1006/layout_recognition.onnx` | 对 90 个棋盘交叉点进行整盘分类 |
| `third_party/models/atopx/large.onnx` | 检测棋盘及红黑棋子，辅助确定类型、颜色和位置 |

这些模型使用 Git LFS 管理。克隆仓库前请安装 [Git LFS](https://git-lfs.com/)：

```bash
git lfs install
git clone <仓库地址>
cd XiangqiAnalyzer
git lfs pull
```

请确认上述 `.onnx` 文件是真实模型，而不是只有几行文本的 LFS 指针文件。

## 另行下载 Pikafish

Pikafish 可执行文件和 NNUE 权重**不包含在仓库中**，需要从官方渠道下载：

- Pikafish 官方发布页：<https://github.com/official-pikafish/Pikafish/releases>
- Pikafish 官方源码：<https://github.com/official-pikafish/Pikafish>
- 官方 NNUE 权重：<https://github.com/official-pikafish/Networks/releases/download/master-net/pikafish.nnue>

请下载适用于 macOS 的 `Pikafish-MacOS-universal` 和 `pikafish.nnue`，放入以下目录：

```text
XiangqiAnalyzer/
└── third_party/
    └── pikafish/
        ├── Pikafish-MacOS-universal
        ├── pikafish.nnue
        ├── Copying.txt
        └── NNUE权重协议（使用视为同意本协议）_NNUE License.txt
```

其中可执行文件和权重必须使用上述文件名。下载后，如果可执行文件没有执行权限，可运行：

```bash
chmod +x third_party/pikafish/Pikafish-MacOS-universal
```

Pikafish 和 NNUE 权重分别受其许可证约束，请仔细阅读随附协议。应用不会从网络自动下载或更新它们。

## 准备 ONNX Runtime

ONNX Runtime 运行库不提交到仓库，请使用官方 macOS Universal2 发布包和项目内的安全提取脚本：

```bash
mkdir -p third_party/downloads
curl -L "https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-osx-universal2-1.23.2.tgz" \
  -o third_party/downloads/onnxruntime-osx-universal2-1.23.2.tgz
/usr/bin/python3 -I tools/extract_onnxruntime.py \
  third_party/downloads/onnxruntime-osx-universal2-1.23.2.tgz \
  third_party/onnxruntime
```

脚本只提取头文件、动态库和许可证，不执行压缩包内的程序。

## 构建

### 1. 安装构建工具

```bash
brew install cmake qt git-lfs
```

### 2. 配置并编译

在项目根目录运行：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
```

构建过程会检查 Pikafish、NNUE 权重、ONNX Runtime 和识别模型是否齐全。缺少必需文件时，配置会给出明确错误。

### 3. 运行测试

```bash
ctest --test-dir build --output-on-failure
```

测试覆盖象棋规则、自动对弈坐标映射、Pikafish 通信、真实棋盘图片识别和主要界面流程。

### 4. 启动应用

```bash
open build/XiangqiAnalyzer.app
```

## 使用说明

### 局势分析

1. 选择“新对局”或进入自由摆棋。
2. 设置行棋方为“红”或“黑”。
3. 点击“开始分析”查看评分、候选着法和主要变化。
4. 单击候选着法显示箭头；点击主要变化中的中文着法可逐步预览。
5. 点击“电脑走棋”执行当前最佳着法。

“分析设置”中可调整分析时长、推荐着法数量、搜索线程、哈希内存和 Pikafish 路径。

### 图片识别

1. 点击“识别棋盘”，或把棋盘图片直接拖入识别窗口。
2. 选择系统截图或导入本地图片。
3. 等待本地模型完成棋盘定位和棋子识别。
4. 对照原图检查棋子、方向和行棋方，必要时手动修改。
5. 点击“校对完成，摆到主棋盘”。

首次使用系统截图时，需要允许“屏幕录制与系统录音”权限。只隐藏棋析自身窗口，不会隐藏其他应用。

### 自动对弈

1. 点击“自动对弈”并选择目标象棋应用窗口。
2. 设置己方执子、开始方式、棋盘方向和每手思考时间。
3. 授予屏幕录制和辅助功能权限后开始。
4. 应用会连续确认稳定局面，校验合法变化后计算并点击走棋。
5. 点击“停止对弈”可随时停止；检测到终局后也会自动停止。

等待对方行棋的最长时间为 3 分钟。终局根据合法着法和本地分析结果判断，不读取画面中的胜负文字。

## 目录结构

```text
src/                         Qt/C++ 应用源代码
tests/                       自动化测试与识别回归测试
resources/                   界面和识别参考资源
third_party/models/          仓库包含的全部 ONNX 识别模型
third_party/onnxruntime/     本地 ONNX Runtime 运行库及许可
third_party/pikafish/        用户另行下载的 Pikafish 和 NNUE 权重
THIRD_PARTY_NOTICES.md       第三方组件、模型来源及授权说明
```

## 隐私与安全

- 棋盘图片和局面只在本机处理。
- 自动对弈只读取和点击用户明确选择的窗口。
- 不需要账号、API Key 或云端识别服务。
- 不应通过第三方下载站获取来路不明的分析组件或模型。

## 第三方组件与授权

模型来源、许可证位置和已知授权限制详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。公开再分发、销售或商业使用前，请确认你拥有相应模型和权重的授权；Pikafish 的 GPLv3 对应源代码义务及 NNUE 协议也需要分别遵守。

## 侵权联系与删除

本仓库尊重原作者及权利人的合法权益。若仓库中的代码、模型、图片或其他内容侵犯了您的权益，请通过 GitHub Issues 与我联系，并说明涉及的具体内容；确认后我会尽快删除或替换相关内容。
