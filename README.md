# FatFishFairy

蓝色大肥鱼具身智能（不是 <!-- keep this line -->

[中文](README.md) | [English](README_EN.md)

Windows 桌面精灵，提供两个程序：

- **FatFishFairy**：显示置顶的透明角色窗口，持续观察屏幕、维护记忆并在气泡中回应；播放动画，支持切换主题、拖动并记住主题和位置。
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

### 配置模型

首次使用 FatFishFairy 或 FatFishCli 前，从模板创建本地配置；已有配置时直接编辑即可：

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

`env/apikey.json` 已被 Git 忽略，不要提交密钥。离线单元测试无需此配置，本地集成测试使用独立的模拟配置。

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
- 启动时在窗口上方显示系统气泡“Hello, world!”，随后自动截取所有显示器，依次运行视觉模型和精灵模型，将精灵发言替换到气泡中，并立即开始下一轮。
- 窗口左下角用天蓝色粗体显示运行进度：`V` 表示正在准备或执行视觉观察，`F` 表示精灵正在回应。没有空闲标记，重试等待期间保留当前阶段。
- 字母后的数字表示自上次成功以来的失败次数，零时省略。上下文超限会立即累加，即使随后恢复，也会显示 `F1`、`F2` 等；其他导致本轮失败的错误计一次。下一轮可能显示 `V2`，切换主题不会清零；完整一轮成功后立即清零，静默成功也一样。
- 气泡的小尾巴向下指向精灵窗口顶部中央，随窗口移动；靠近屏幕边缘时，会调整气泡位置以保持可见。最新发言保持显示到下一次更新；精灵选择不发言时，清空并隐藏气泡，直到有新文字。
- 每轮非空的完整发言会追加到 `env/history.md`，不存在时自动创建。每条记录以 `# Speak YYYY-MM-DD HH-mm-ss` 开头，空一行后保存发言正文，使用本机时间和 UTF-8 编码；重启后继续追加。该文件已被 Git 忽略，不记录启动问候、静默结果或错误提示；写入失败会通过气泡报错并重试。此记录功能仅用于 FatFishFairy。
- 模型配置或调用失败时，气泡显示以“调用大模型发生错误：”开头的错误信息，一秒后自动重试。修正缺失或无效的模型配置后，程序会重新尝试初始化。
- 按住角色左键拖动，松开后保存位置；下次启动恢复该位置。
- 右键打开菜单，在“主题”子菜单中切换角色主题，或选择“退出”关闭程序。主题按 `themes/theme.json` 中的顺序显示中文名称，当前主题带有勾选标记。
- 切换主题后立即从新主题中随机选择一组动画，从第一帧开始播放，并保存选择供下次启动恢复。未保存主题或已保存的主题不存在时，使用列表中的第一个主题。
- 精灵性格由 `themes/<当前主题>/Character.md` 决定；文件不存在时使用 `themes/loli_maid/Character.md`。切换到其他主题会丢弃原精灵的对话，下一轮使用新主题开始全新会话，保留 `memory` 中的记忆和发言记录。正在进行的一轮会继续使用该轮开始时的主题，完成后仍可能按原性格发言。再次选择当前主题不会重置会话。
- 在当前主题中随机选择动画，以每秒一帧的速度完整播放三遍，再随机选择下一组。

内置两个主题，共 20 组动画、70 帧，可通过右键菜单切换：

- **萝莉小妹抖**：Q 版角色，共 10 组、34 帧，包含喝咖啡、做作业、读漫画、睡觉、玩耍、编程、绘画和变成鲸鱼等动画。
- **长大的妹抖**：正常成人比例的角色，共 10 组、36 帧，包含喝咖啡、刷手机、睡觉、编程、绘画、上课，以及鲸鱼盔甲女战士和鲸鱼水手服变身等动画。

萝莉小妹抖使用 `themes/loli_maid/Character.md` 中贪吃、爱摸鱼、傲娇毒舌的原有性格；长大的妹抖使用 `themes/grown_maid/Character.md` 中知性、温柔体贴、时常关心和鼓励主人的女仆姐姐性格，带有略微夸张的二次元语气，只自称“我”、称呼用户“主人”，不会临时起外号。在右键菜单中选择“主题 → 长大的妹抖”即可使用。

`env/Character.md` 不再使用。性格文件已存在但为空或无法读取时会报错，修正文件后桌面程序会重试。

窗口位置和主题选择保存在 `env/config.json`，通常无需手动创建。也可以在程序关闭后编辑：

```json
{
  "windowX": 0,
  "windowY": 0,
  "selectedTheme": "loli_maid"
}
```

文件或坐标缺失时坐标默认为零，支持其他显示器上的负坐标。`selectedTheme` 使用 `themes/theme.json` 中的主题目录键，必须是字符串；缺失或没有完全匹配的键时使用第一个主题。拖动和切换主题会创建或更新文件，并保留彼此的设置及其他字段；该文件已被 Git 忽略。配置格式错误、非法坐标、主题选择类型错误或主题图片缺失、无效时，程序会报错。

桌面程序持续截屏并请求配置的模型服务。截图在内存中处理，发送给视觉模型；精灵模型收到完整观察描述及该轮观察结束时的本机日期时间。视觉模型每轮使用新会话，精灵模型在同一主题下连续保留对话，并可维护 `memory` 中的记忆文件。模型请求期间仍可拖动、切换主题和退出；退出会取消未完成的请求并停止循环。

## 使用 FatFishCli

配置模型后，在终端运行：

```powershell
& ./FatFish/x64/Debug/FatFishCli.exe
```

- **ENTER**：截取所有显示器，先运行视觉模型，再将完整观察描述交给精灵模型。
- **ESC**：退出。当前一轮执行期间，按键会在该轮结束后处理。

CLI 窗口标题为 `FatFishCli`。每次按 ENTER 执行一轮，不会自动连续截屏。截图在内存中处理并发送给配置的视觉模型服务；精灵模型接收观察文字，以及该轮视觉观察结束时的本机日期时间（`YYYY-MM-DD HH-mm-ss`，24 小时制）。视觉模型每轮使用新会话，精灵模型在同一进程中保留对话和各轮时间，可维护 `memory` 中的文件，也可以选择不发言。

CLI 始终使用 `themes/loli_maid/Character.md` 作为精灵性格，与桌面程序保存的主题选择无关。

其他启动方式：

```powershell
# 执行一轮截屏和模型请求后退出
& ./FatFish/x64/Debug/FatFishCli.exe --once

# 使用另一目录下的 env、memory 和 themes
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

其他工具调用和普通回复以 `Vision> JSON` 或 `Fairy> JSON` 显示，便于诊断。网络或配置错误会终止 CLI；精灵对话过长时会先按下述规则恢复，恢复失败后交互式 CLI 仍可等待下一次 ENTER。

记忆文件保存在 `memory`，重启后会重新加载；完整对话只在当前进程中保留。模型的文件工具只能访问记忆目录，该目录已被 Git 忽略。工具说明见 [env/Tools.md](env/Tools.md)。

## 对话过长时

两个程序都会在精灵模型明确返回“上下文过长”错误时自动恢复。以本轮首次出错时已有的完整历史轮数为基准，从最旧的轮次开始处理，每轮观察及其回复、工具结果一起删除：

1. 第一次出错，删除最旧的三分之一历史轮次，数量向上取整，然后重试。
2. 第二次出错，继续删除，直到累计删去原历史轮数的三分之二，数量向上取整，然后重试。
3. 第三次出错，清空精灵对话，包括本轮的工具交互和未完成发言，仅用本轮原始观察重新请求精灵。
4. 第四次仍然出错，结束本轮。桌面程序显示错误，一秒后重新截屏并开始下一轮；交互式 CLI 等待下一次 ENTER，`--once` 则以非零状态正常退出。

前三次重试都保留本轮原始观察的完整文字和时间，不重新运行视觉模型。前两次还保留本轮已经完成的工具交互和发言，避免重复执行；第三次重新开始精灵回应。`memory` 中已保存的记忆和 `env/history.md` 中已有的发言记录会保留。此恢复只用于精灵模型的上下文过长错误，其他错误仍按原方式处理。

## 测试

### 离线单元测试

构建后运行完整测试集：

```powershell
Push-Location FatFish
& "$PWD/../Release/.github/Scripts/copilotExecute.ps1" -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64
Pop-Location
```

测试使用模拟模型回复和临时目录，不读取真实密钥、不截屏、不联网。覆盖记忆文件安全、配置、流式响应、输出格式、错误反馈、多轮模型流程、上下文过长时分阶段删减与重试，以及窗口位置、主题顺序、主题选择与回退、切换主题后重建精灵会话并保留记忆、设置保存、动画播放逻辑和发言记录的追加、时间格式与写入失败处理。完整测试集应通过，Debug 构建应无内存泄漏。

### 本地集成测试

构建对应程序后，在 PowerShell 7 中运行：

```powershell
# CLI：实际截图、PNG 编码、本机 HTTP 请求、记忆写入和上下文过长重试
& ./FatFish/UnitTest/Invoke.ps1 -Configuration Debug -Platform x64

# 桌面窗口：动画与交互、连续模型调用、气泡更新、错误恢复、请求期间退出
& ./FatFish/UnitTest/Invoke-Fairy.ps1 -Configuration Debug -Platform x64
```

两个集成测试都只将显示器截图发送到本机回环测试服务器，不保存这些截图、不访问真实模型服务。CLI 测试还会返回真实的 HTTP 400 上下文过长错误，验证精灵保留当前工具结果并重试，没有重复执行工具或发言。桌面集成测试使用临时程序、主题、配置和记忆目录，检查透明置顶、动画、气泡跟随、拖动与位置恢复、主题菜单顺序、切换保存、启动恢复及无效主题键的回退。切换主题时，测试验证当前一轮继续使用原性格，下一轮清空精灵对话；同时检查连续模型调用、发言更新与静默、错误显示与恢复，以及请求尚未完成时通过菜单退出。测试不读取真实配置或密钥；可加上 `-ScreenshotPath PATH` 保存测试窗口、菜单和气泡截图，目视检查中文主题名称、勾选标记和完整发言。长发言的实际屏幕截图以 `.speech-desktop.png` 结尾。测试会移动鼠标，结束后恢复指针位置。

桌面集成测试还会在模型请求暂停时检查左下角进度的天蓝色、位置和状态变化，验证 `F1`、`F2`、`V1` 以及成功后的清零。使用 `-ScreenshotPath` 时，会额外保存 `.progress-*.png` 实际屏幕裁剪图，用于目视确认字母和数字。

### 真实模型验证

完成模型配置后，启动一个交互式 `FatFishCli` 进程，连续完成 **10 轮 ENTER**，每轮等待视觉模型和精灵模型成功结束，再按 **ESC** 确认正常退出。若有一轮失败，修复后重新开始 10 轮验证。此验证会截屏并请求配置的真实模型服务。

还需使用配置的真实模型启动一个 `FatFishFairy` 进程，确认**启动后两分钟内至少有两次非空精灵发言实际显示在气泡中**。逐次检查屏幕上的气泡更新；仅解析到 `speak` 调用、记录日志或通过本机模拟服务测试不满足此要求。完成后通过菜单退出，确认正常关闭。若未达到要求，修复后重新开始桌面验证。

默认性格会避免评论精灵自身，也不会反复点评静止画面。验证气泡发言时，请展示与精灵无关、随操作变化的桌面内容。

修改共享 `Agents` 代码时需要完整验证，并包含桌面集成测试；修改单个应用时验证受影响的应用。修改项目配置时，需要构建并运行 `Debug` / `Release` × `Win32` / `x64` 的单元测试。仅修改文档或主题资源而未改动代码、项目配置时，检查变更内容即可，无需构建或运行上述验证。
