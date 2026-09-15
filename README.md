# FatFishFairy

蓝色大肥鱼具身智能（不是 <!-- keep this line -->

[中文](README.md) | [English](README_EN.md)

Windows 桌面精灵，提供两个程序：

- **FatFishFairy**：显示置顶的透明角色窗口，播放动画，支持拖动并记住位置。目前尚未接入模型，不需要 API 密钥。
- **FatFishCli**：按键触发屏幕观察。视觉模型描述所有显示器的截图，精灵模型根据描述维护记忆并在终端回应。

## 准备

### 开发环境

- Windows。
- Git、PowerShell 7。
- Visual Studio 或 Microsoft C++ Build Tools，安装 C++ 桌面开发组件、**v145 工具集**和最新 Windows 10 SDK。项目使用 C++20。

获取仓库和 GacUI 依赖：

```powershell
git clone --recurse-submodules https://github.com/vczh/FatFishFairy.git
Set-Location FatFishFairy
```

已有仓库可在根目录运行以下命令，初始化依赖并更新到 `Release` 的最新 `master`：

```powershell
git submodule update --init --recursive
git submodule update --remote Release
```

以下命令均从仓库根目录的 PowerShell 7 中运行。构建脚本要求设置 `VLPP_VSDEVCMD_PATH`；将示例路径替换为所安装 Visual Studio / Build Tools 的 `VsDevCmd.bat` 路径：

```powershell
$env:VLPP_VSDEVCMD_PATH = 'C:\path\to\Visual Studio\Common7\Tools\VsDevCmd.bat'
```

该设置仅在当前 PowerShell 会话中生效。

### 准备资源编译工具

首次构建前，如果缺少 `Release/Tools/GacGen.exe` 或 `Release/Tools/CppMerge.exe`，先构建并复制它们：

```powershell
Push-Location Release/Tools/Executables
& "$PWD/../../.github/Scripts/copilotBuild.ps1" -Configuration Release -Platform x64
Copy-Item ./x64/Release/GacGen.exe, ./x64/Release/CppMerge.exe ../
Pop-Location
```

桌面项目在构建时会自动调用这些工具生成 UI 资源。工具的其他构建方式见 [Release/Tools/README.md](Release/Tools/README.md)。

### 配置模型（仅 FatFishCli 需要）

首次使用 CLI 前，从模板创建本地配置；已有配置时直接编辑即可：

```powershell
Copy-Item env/apikey-template.json env/apikey.json
```

编辑 `env/apikey.json`：

| 字段 | 内容 |
| --- | --- |
| `apikey` | 模型服务的 API 密钥。 |
| `url` | API 基址，例如 `https://your-server/v1`，或完整的 `/chat/completions` 地址。不要填写 `/models` 地址；服务地址应为无需重定向的最终地址。 |
| `auth_header` | 认证头，例如 `Authorization: Bearer $APIKEY`；`$APIKEY` 会替换为上述密钥。 |
| `vision_model` | 用于屏幕观察的模型 ID，需支持图像输入和工具调用。 |
| `fairy_model` | 用于精灵回应和记忆的模型 ID，需支持工具调用。 |

服务需兼容 OpenAI v1 Chat Completions 协议。两个角色分别配置模型；也可以填写相同的模型 ID。旧配置中的 `chat_model` 仍可作为 `fairy_model` 的别名，两者同时出现时必须相同。

`env/apikey.json` 已被 Git 忽略，不要提交密钥。桌面程序和离线单元测试无需此配置。

## 构建

完成准备后，可在 Visual Studio 中打开 `FatFish/FatFish.sln` 构建整个解决方案，或运行：

```powershell
Push-Location FatFish
& "$PWD/../Release/.github/Scripts/copilotBuild.ps1" -Configuration Debug -Platform x64
Pop-Location
```

支持 `Debug` / `Release` 和 `Win32` / `x64` 的四种组合，修改命令参数即可。Debug 构建启用内存泄漏检查。

| 平台 | 输出目录 |
| --- | --- |
| x64 | `FatFish/x64/<Configuration>/` |
| Win32 | `FatFish/<Configuration>/` |

目录中包含 `FatFishFairy.exe`、`FatFishCli.exe` 和 `UnitTest.exe`。以下启动示例使用 Debug x64 构建。保留仓库的目录布局：程序根据可执行文件的位置查找配置、记忆或主题，不依赖启动时的工作目录。

## 使用 FatFishFairy

```powershell
& ./FatFish/x64/Debug/FatFishFairy.exe
```

- 显示 384×384 的透明无边框角色窗口，始终置顶。
- 启动时在窗口上方显示系统气泡“Hello, world!”，随窗口移动并保持显示，直到程序关闭；靠近屏幕边缘时，会调整气泡位置以保持可见。
- 按住角色左键拖动，松开后保存位置；下次启动恢复该位置。
- 右键打开菜单，选择“退出”关闭程序。
- 使用 `themes/theme.json` 中的第一个主题，随机选择动画，以每秒一帧的速度完整播放三遍，再随机选择下一组。

内置主题包含喝咖啡、做作业、读漫画、睡觉、玩耍、编程、绘画和变形等动画，共 10 组、34 帧。

窗口位置保存在 `env/config.json`，通常无需手动创建。也可以在程序关闭后编辑：

```json
{
  "windowX": 0,
  "windowY": 0
}
```

文件或坐标缺失时默认为零，支持其他显示器上的负坐标。拖动会创建或更新文件并保留其他字段；该文件已被 Git 忽略。配置格式错误、非法坐标或主题图片缺失、无效时，程序会报错。

目前桌面窗口播放动画并显示固定的启动问候，不自动截屏或调用模型。屏幕观察和模型回应通过下述 CLI 使用。

## 使用 FatFishCli

配置模型后，在终端运行：

```powershell
& ./FatFish/x64/Debug/FatFishCli.exe
```

- **ENTER**：截取所有显示器，先运行视觉模型，再将完整观察描述交给精灵模型。
- **ESC**：退出。当前一轮执行期间，按键会在该轮结束后处理。

每次按 ENTER 执行一轮，不会自动连续截屏。截图在内存中处理并发送给配置的视觉模型服务；精灵模型接收观察文字。视觉模型每轮使用新会话，精灵模型在同一进程中保留对话，可维护 `memory` 中的文件，也可以选择不发言。

其他启动方式：

```powershell
# 执行一轮截屏和模型请求后退出
& ./FatFish/x64/Debug/FatFishCli.exe --once

# 使用另一目录下的 env 和 memory
& ./FatFish/x64/Debug/FatFishCli.exe --repo-root 'C:\path\to\FatFishFairy'

# 查看参数
& ./FatFish/x64/Debug/FatFishCli.exe --help
```

视觉观察和精灵发言以 `Vision (speak)>` 或 `Fairy (speak)>` 文本块显示，例如：

```text
Vision (speak)>
****************
屏幕观察内容
****************
```

其他工具调用和普通回复以 `Vision> JSON` 或 `Fairy> JSON` 显示，便于诊断。网络或配置错误会终止 CLI。

记忆文件保存在 `memory`，重启后会重新加载；完整对话只在当前进程中保留。模型的文件工具只能访问记忆目录，该目录已被 Git 忽略。工具说明见 [env/Tools.md](env/Tools.md)。

## 测试

### 离线单元测试

构建后运行完整测试集：

```powershell
Push-Location FatFish
& "$PWD/../Release/.github/Scripts/copilotExecute.ps1" -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64
Pop-Location
```

测试使用模拟模型回复和临时目录，不读取真实密钥、不截屏、不联网。覆盖记忆文件安全、配置、流式响应、输出格式、错误反馈、多轮模型流程，以及窗口位置和动画播放逻辑。完整测试集应通过，Debug 构建应无内存泄漏。

### 本地集成测试

构建对应程序后，在 PowerShell 7 中运行：

```powershell
# CLI：实际截图、PNG 编码、本机 HTTP 请求和记忆写入
& ./FatFish/UnitTest/Invoke.ps1 -Configuration Debug -Platform x64

# 桌面窗口：透明置顶、动画、气泡跟随、拖动、位置恢复和菜单退出
& ./FatFish/UnitTest/Invoke-Fairy.ps1 -Configuration Debug -Platform x64
```

CLI 集成测试只将截图发送到本机回环测试服务器，不保存图片、不访问真实模型服务。桌面集成测试使用临时程序、主题和配置，不读取真实配置或密钥；测试会移动鼠标，结束后恢复指针位置。

### 真实模型验证

完成模型配置后，启动一个交互式 `FatFishCli` 进程，连续完成 **10 轮 ENTER**，每轮等待视觉模型和精灵模型成功结束，再按 **ESC** 确认正常退出。若有一轮失败，修复后重新开始 10 轮验证。此验证会截屏并请求配置的真实模型服务。

修改共享 `Agents` 代码时需要完整验证，并包含桌面集成测试；修改单个应用时验证受影响的应用。修改项目配置时，需要构建并运行 `Debug` / `Release` × `Win32` / `x64` 的单元测试。仅修改文档或主题资源而未改动代码、项目配置时，检查变更内容即可，无需构建或运行上述验证。
