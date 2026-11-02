# Windows 中文便携版

本分支 Windows x64 便携包包含 Qt 6.11.3、简体中文界面、离线说明和固定版本的 SuperTiled2Unity 2.4.0。它不是上游官网安装包，也不是数字签名安装器。

## 打开

1. 从本分支通过全部验证的 GitHub Actions 运行中下载 `Tiled-Windows-x64-Qt6.11.3-unsigned`。
2. 解压外层产物，再完整解压其中的应用 ZIP 到你可写入的目录，例如“文档\\Tiled”。不要只取出 `tiled.exe`，也不要在 ZIP 内直接双击运行。
3. 打开 `tiled.exe`。不需要安装 Qt、Python、Git 或开发工具。设置保存在旁边的 `tiled.ini`；不要把该目录放在需要管理员权限的位置。
4. 系统可能提示来源或签名风险：本包未使用 Authenticode 证书签名。核对来源和校验和，遵守系统或组织的安全策略；不要关闭 Defender、SmartScreen 或绕过组织限制。

适用目标是 Windows 10/11 的 x64 桌面环境；不提供 32 位或 Windows ARM 原生包。实际 CI 使用标准 Windows Server 2022 x64 runner，不能替代每一款用户硬件验证。

可在 PowerShell 中运行 `Get-FileHash .\Tiled-Windows-x64-Qt6.11.3-unsigned.zip -Algorithm SHA256`，与同目录 `.sha256` 文本比较。`fork-build.json` 记录准确提交、Qt 版本和 DLL 校验。校验和检测文件传输损坏，不替代发布者数字签名。

## 从 Tiled 送入 Unity

先保存所有打开的地图、瓦片集和模板，关闭目标 Unity 项目的编辑器。使用 **文件 → 导入到 Unity…**，选择 Unity 2022.3 项目根目录，在预览中核对文件清单后确认。取消不会写入项目。

功能将官方导入器和当前地图依赖送入受控目录，不修改项目的 `manifest.json`，也不覆盖不属于本工具管理的文件。复制成功只表示文件已送入：随后用 Unity 打开项目并等待其完成导入，检查 Console。Unity 首次解析其他包依赖可能需要联网。

详细格式限制和错误处理见 `doc/unity-import-zh_CN.md`；常用操作见 **帮助 → 中文使用说明**。此交付验证覆盖 Tiled 侧界面和文件转移，未宣称通过 Unity Editor 引擎编译或所有地图格式。

## 排错

- 缺少 DLL：重新完整解压经校验的 ZIP，不要从不明网站下载单个 DLL。
- 想保留旧版：关闭程序后将新版本解压到新目录，不要混合两套 Qt DLL。
- 界面语言可在首选项中切换；系统原生安全提示语言仍由 Windows 决定。
- 运行日志中的编译器、系统或第三方原始错误可能为英文，报告时请保留原文，并附 `fork-build.json`。
- 微软 VC 运行库由所选 Visual Studio 的允许再分发目录随应用提供；更新此便携包时应一并更新运行库，不能仅替换 EXE。

## 构建验证说明

`dist/windows/build-package.ps1` 在全新目录用同一套 Qt 6.11.3 构建和部署。发布前运行对应 Qt 的 C++ 回归、真实 Windows Qt 界面导入流程、PE 依赖与固定文件审计，并从最终 ZIP 解压到带中文和空格的新路径，在不含开发套件的 PATH 下展示地图、核对实际加载 DLL、持续运行 15 分钟并正常退出。

Python、Qbs 和 Visual Studio 仅用于构建/验证，不是用户运行要求。诊断程序、QtTest 和调试符号不应出现在成品中。源码许可证及官方导入器署名保留；源码见 https://github.com/juliansoul250/tiled/tree/fix/layer-stability-ui 。
