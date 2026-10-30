# Tiled 简体中文版：安装、验证与源码交接

源码仓库：<https://github.com/juliansoul250/tiled>
工作分支：**`fix/layer-stability-ui`**
基于 Tiled v1.12.2；原作者、贡献者署名、AUTHORS、COPYING/GPL 和源码许可证均保留。

## 给使用者和秘书的说明

本版沿用官方简体中文翻译，并补齐此版本缺失的界面、属性、提示和错误说明。新配置或原先“跟随系统”的配置默认使用简体中文；明确选择过其他语言的设置不会被强行覆盖。可在首选项的语言列表中选择中文，或“默认（简体中文）”。如果旧界面仍为英文，按 **Preferences → Interface → Language** 查找。

**“帮助 → 中文入门与故障排查”** 打开随应用附带的离线说明。其源码是 [中文使用说明](tiled-zh_CN-guide.html)。另保留 **“官方完整手册（英文）”**，因为尚未核实有完整的官方在线简体中文手册；不要把本地入门说明冒充官方完整手册译本。

地图、项目、图层、文件名、属性值、代码及脚本标识符属于用户数据，不会被自动翻译。macOS 系统面板、第三方扩展、外部网站及原始编译器诊断可能仍显示英文。

### 安装正确的 Mac 包

1. 进入该分支的 **Validate layer fixes on macOS and Windows** 工作流，核对提交编号，确认 **macOS 15、macOS 27、Windows 和 `macos-package` 全部通过**。本次中文化改变了源码和翻译，不能用旧英文版的通过记录替代。
2. 下载附件 **`Tiled-macOS-Qt6.11.3-ad-hoc`**，解压得到应用 ZIP 和 SHA256 文件。此构建目标是 **Apple Silicon / arm64（M 系列芯片）**；Intel Mac 没有通过本分支的设备验证。
3. 在下载目录执行：
   ```sh
   shasum -a 256 Tiled-macOS-Qt6.11.3-ad-hoc.zip
   ```
   把散列值与附带 `.sha256` 文件的第一段逐字比较。校验文件可能包含 CI 机器上的绝对路径，请不要直接照搬那个路径。
4. 退出旧版、保留备份，再把新 `Tiled.app` 复制到目标“应用程序”文件夹。确保启动的是新副本，而不是旧 Dock 快捷方式或构建目录里的程序。
5. 在 Tiled 应用菜单（或帮助菜单）的“关于 Tiled”中核对 Qt 运行版本与编译版本均为 **6.11.3**。Finder 中“显示包内容”后的 `Contents/Resources/fork-build.json` 记录提交编号、构建系统、架构、Qt 版本与临时签名状态。
6. 先用项目备份或示例进行打开、保存、关闭、重新打开，再继续正式工作。

> **安全与签名：** 这是临时（ad-hoc）签名的应用 ZIP，**没有 Apple Developer ID 签名或公证，不是已公证安装器**。若系统阻止打开，请停止并保留提示；不要关闭或绕过 Gatekeeper、SIP、签名验证等保护。本流程不索取 Apple 账号或签名证书。

## Mac：从源码构建

安装 Xcode / Command Line Tools、Qbs（可用 `brew install qbs`）和官方完整 **Qt 6.11.3 macOS 桌面套件**。需要 Widgets、QML、SVG、图像格式以及官方中文翻译资源；套件中必须有 `translations/qtbase_zh_CN.qm`。不要把不同版本的 Qt 框架、插件或部署工具混用。

```sh
git clone --branch fix/layer-stability-ui --single-branch \
  https://github.com/juliansoul250/tiled.git
cd tiled
git rev-parse HEAD
export QT_ROOT_DIR="$HOME/Qt/6.11.3/macos"  # 改为实际套件路径
bash dist/macos/build-fork-package.sh
```

脚本只接受 Qt 6.11.3，使用同一个绝对套件路径下的 `qmake` 和 `macdeployqt`；每次建立新的临时构建和安装目录，生产构建不启用内部控件符号导出。它会：

- 部署 Qt 和插件，把调试符号单独保留，不混进运行插件目录；
- 验证每个 Qt 框架的完整版本及 UUID、Cocoa 插件与套件的一致性；
- 检查实际动态依赖必须存在，禁止外部开发机路径；相对搜索提示必须保持在应用包内；
- 打包 Tiled 简中词典、同套件 Qt 官方简中词典及离线中文说明，检查搜索路径与词典字节一致性；
- 写入构建身份，临时签名并验证签名；
- 把应用复制到构建树之外，直接启动该副本，实际加载中文界面、显示示例地图、截图并正常退出，最后生成 ZIP 与校验值。

本地运行保留你的语言设置；如果此前明确选择过英文，请先切换简体中文再进行中文包验证。临时目录会显示在输出中，并保留供排查。CI 额外要求部署后的应用持续运行 **900 秒**。

只做开发调试、不打便携包时，可使用：

```sh
qbs setup-toolchains --detect
qbs setup-qt "$QT_ROOT_DIR/bin/qmake" tiled-qt
qbs config profiles.tiled-qt.baseProfile xcode
qbs build profile:tiled-qt config:release products.python.condition:false
qbs run profile:tiled-qt config:release products.python.condition:false -p tiled
```

如果检测到的 Apple 编译器配置不叫 `xcode`，请改为实际名称。Qt 套件与编译器必须匹配目标架构。不要把未经部署的构建目录 `.app` 当成可分发成品。

## Windows：保留原有兼容构建

使用 Visual Studio 2022 C++ 桌面工具、Qbs 和 Qt **6.8.3 `msvc2022_64`** 完整套件（包含需要的开发头文件及简中词典）。在 Developer PowerShell 中执行，不要混用 MinGW 与 MSVC 库。

```powershell
$QtDir = 'C:\Qt\6.8.3\msvc2022_64'
$env:PATH = "$QtDir\bin;$env:PATH"
qbs setup-toolchains --detect
qbs setup-qt "$QtDir\bin\qmake.exe" tiled-qt
# 改为 qbs config --list 中实际检测到的 MSVC x64 配置。
qbs config profiles.tiled-qt.baseProfile MSVC2022-x64
qbs build profile:tiled-qt config:release products.python.condition:false
qbs run profile:tiled-qt config:release products.python.condition:false -p tiled
```

Windows 在本流程中验证源码构建、图层和绘制回归；**没有因此声称已交付新的 Windows 安装器**。macOS 不得改回历史 Qt 6.8.3 套件。

## 验证内容与边界

- 静态检查：当前简中词条不得未完成或为空，格式占位符及 HTML 标签保持一致；新增错误与版本诊断也必须有译文。
- 原生测试：真实 Cocoa 窗口、重命名确认/取消、图层菜单、四种图层、嵌套删除/撤销/重做、文档切换、浮动/停靠、原生材质及不透明回退。
- 保存回归：普通同路径保存 → 重新读取 → 修改 → 再保存/读取；这不冒充完整“另存为”文件面板自动化。
- 持续测试：在 macOS 27 的单一进程中反复编辑并主动访问辅助功能表格缓存至少 **15 分钟**。新版本仍需重新运行，不复用旧英文版的源码通过状态。
- 中文运行检查：读取实际编译的应用和 Qt 字典，检查中文菜单及按钮译文；真正的主应用必须报告两个中文词典成功加载，并留下中文桌面截图。
- 部署验证：正常生产 `.app` 脱离构建树直接启动，在示例地图上保持 **900 秒**，然后正常退出。此处是持续运行测试，不声称在打包后的主应用中自动编辑了 15 分钟。
- CI 仅临时调整独立托管账号的“降低透明度”偏好，并在结束时恢复、读回核对，不修改用户 Mac 的系统设置。

所有编译错误、超时、断言、崩溃、未暴露的原生窗口、异常退出或错误的包依赖都会使对应验证失败。Qt 的正常 Quit AppleEvent 可能返回 `-128`；只有独立等待到实际进程退出码 **0** 才算正常退出，强制结束不算通过。

保留真正的原生侧栏材质（不是 macOS 26 Liquid Glass）；地图画布保持不透明。浮动面板与系统“降低透明度”有意采用不透明回退。打赏入口和提醒仍已移除，版权与许可证保持不变。

## 历史证据（不是本次中文验收结果）

- [41031506290](https://github.com/juliansoul250/tiled/actions/runs/41031506290)：早期 Qt 6.8.3 材质截图，整体为失败记录，不允许据此重新安装旧套件。
- [41036743240](https://github.com/juliansoul250/tiled/actions/runs/41036743240)：选定完整 Qt 6.11.3；旧 6.8.3 在原生辅助功能压力测试中重现崩溃。
- [41049665327](https://github.com/juliansoul250/tiled/actions/runs/41049665327)：`35ee147f…` 三个平台源码测试通过，其中 macOS 27 单进程持续编辑 160 轮、901 秒；当时包审计解析器尚未通过。
- [41054546967](https://github.com/juliansoul250/tiled/actions/runs/41054546967)：`085e5fdd…` 英文阶段最终修复包通过，包含 900 秒真实部署运行与正常退出。本次中文源码变更必须取得新的完整通过记录。

旧的“unchanged-source macOS package”工具仅保留为手动触发，仍严格固定旧源码基线；它不能用来跳过本次中文版本的源码测试。通用上游 `packages.yml` 也不是这份中文修复版的交付依据。

已知无关的原始 Properties 全套测试问题仍见 `layer-stability-validation.md`。有限测试不能保证所有硬件、Intel Mac、物理 Retina/GPU 组合、系统版本或第三方扩展永不崩溃。报告问题时请附 `.ips`、准确提交与 Qt 版本、macOS 版本及最后操作。

### 托管测试环境维护

GitHub 官方公告 macOS 14 托管镜像将于 2026 年 11 月 2 日退役，退役前减少容量并安排停机演练。因此本次中文版本的兼容性测试改用标准 `macos-15`，保持相同 Qt 6.11.3、原生界面和中文断言；用户系统对应的 `xcode-27` 与全部 15 分钟验证不变。旧版本 macOS 14 的历史通过记录仍保留，但不能当作本次中文版本在 macOS 14 上通过。首次中文构建的两次 macOS 14 作业均未分配运行器，未执行任何测试。

参考：[GitHub 官方 macOS 14 镜像退役公告](https://github.blog/changelog/2026-10-01-github-actions-macos-14-runner-image-retirement/)。
