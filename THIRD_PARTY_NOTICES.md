# 第三方组件与模型说明

## Pikafish 2026-09-25 与 NNUE 权重

- 官方项目与对应源代码：https://github.com/official-pikafish/Pikafish
- 用途：本机离线局面分析与推荐着法，通过标准 UCI 协议作为独立进程运行。
- 内置文件：`third_party/pikafish/Pikafish-MacOS-universal` 和 `pikafish.nnue`。
- Intel 安装包使用 `Pikafish-MacOS-x86_64-avx2`，由官方源码提交 `b562d6aeac5401879e973dc53ddb56053f07bb6a`（2026-09-23，对应 2026-09-25 发布）以 `ARCH=x86-64-avx2` 编译，避免原通用程序在 Rosetta 下进入不兼容的 AVX-512 路径。
- 程序许可证：GNU GPLv3，完整文本位于 `third_party/pikafish/Copying.txt`，应用包内为 `Resources/engine/COPYING.txt`。
- NNUE 权重使用协议：`third_party/pikafish/NNUE权重协议（使用视为同意本协议）_NNUE License.txt`，应用包内为 `Resources/engine/NNUE-LICENSE.txt`。

**分发提醒：** 对外提供程序时须遵守 GPLv3 的对应源代码提供要求。NNUE 协议明确禁止未经允许的商业用途；个人本地打包不等于取得商业或公开再分发授权。

## ONNX Runtime 1.23.2

- 项目：https://github.com/microsoft/onnxruntime
- 官方 macOS universal2 包：https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-osx-universal2-1.23.2.tgz
- 许可证：MIT。完整许可证位于 `third_party/onnxruntime/LICENSE`，第三方声明位于同目录的 `ThirdPartyNotices.txt`。
- 用途：在本机 CPU 上执行棋盘四角定位、YOLO 棋子检测和 90 格分类，不需要 Python，也不会把图片发送到外部服务。

## TheOne1006 中国象棋识别模型

- 原项目：https://github.com/TheOne1006/chinese-chess-recognition
- 精简推理镜像：https://github.com/cheese2020/Chinese-Chess-Recognition
- 本次 ONNX 文件及 macOS 预处理参考：https://github.com/sunqinji666-dotcom/xiangqi-assistant
- 模型：
  - `pose.onnx`：棋盘四角 RTMPose，SHA-256 `fbd895d40ab81b41b8188f881866485678b7871775821aa50cfe74004a1dc796`。
  - `layout_recognition.onnx`：90 格 SwinV2 分类，SHA-256 `da66ba9809f15127f8ae729b1755e42ee61c100c4f9979ce0ef13602ac471298`。

**模型授权提醒：** 原 TheOne1006 GitHub 仓库在本次集成时没有明确的 LICENSE 文件；macOS 参考项目的第三方声明也没有把其 MIT 许可证扩展到这两个模型。因此，目前将它们用于本机个人工具的技术验证。对外发布、销售或再分发包含这些权重的应用前，必须另行向模型作者确认授权。ONNX Runtime 的 MIT 许可证不覆盖模型权重。

## atopx/chessboard YOLO 棋子检测模型

- 项目：https://github.com/atopx/chessboard
- 来源：发布版 `v0.1.2` 的 `xqlink_0.1.2_aarch64.dmg`。模型以 ONNX 数据嵌入发布程序，本次仅静态提取权重，没有运行下载的应用。
- 本地文件：`third_party/models/atopx/large.onnx`，应用包内名为 `piece_detection.onnx`。
- SHA-256：`7f0fe6ea57e327b9493a8d85ce11b1762bd53d47769aeed79877f2bb1d6a6795`。
- 用途：640×640 RGB 输入，检测棋盘和 14 类红黑棋子；应用自行完成置信度过滤、NMS、检测中心网格映射和结果复核。
- 归属：atopx/chessboard 作者及相应贡献者。源仓库附带的 Apache License 2.0 已复制到 `third_party/models/atopx/LICENSE`。

**授权提醒：** 源仓库包含 Apache-2.0 许可证，但其 README 同时写有“严禁任何商业化或非法用途”。本说明不把 README 的额外表述解释为已经明确授予模型商业再分发权；对外发布、销售或再分发权重前，应向作者确认模型权重及训练数据的授权范围，并保留许可证和归属说明。

## 样图

`tests/data/board-popup.png` 来自用户在需求中提供的棋盘截图，仅用于本地识别回归测试，不作为公开数据集再分发。
