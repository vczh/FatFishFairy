# FatFishFairy
蓝色大肥鱼具身智能（不是

Windows C++20 桌面精灵原型。`FatFishCli` 按 ENTER 截取所有显示器，先由独立的视觉模型描述屏幕，再由保留会话的精灵模型维护记忆并回应；按 ESC 退出。模型请求顺序执行，每次按键运行一轮。

首次准备：

```powershell
git submodule update --init --recursive
Copy-Item env/apikey-template.json env/apikey.json
```

编辑 `env/apikey.json` 中的 `apikey`、`url`、`auth_header`、`vision_model` 和 `fairy_model`。旧模板的 `chat_model` 仍可作为 `fairy_model` 的别名；同时填写时值必须相同。`url` 支持 API 基址（例如 `https://your-server/v1`）或完整的 `/chat/completions` 地址；查询模型列表的 `/models` 地址不能用作基址。`auth_header` 中的 `$APIKEY` 会替换为密钥。两个角色分别选择模型并管理独立会话，也可将两个配置项指向同一模型 ID；服务器需兼容 Chat Completions、图像输入和函数工具调用。该配置已被 Git 忽略；不要提交密钥。

构建需要 Visual Studio / Build Tools 的 v145 工具集和最新 Windows 10 SDK。打开 `FatFish/FatFish.sln`，或在仓库根目录运行：

```powershell
Set-Location FatFish
& "$PWD/../Release/.github/Scripts/copilotBuild.ps1" -Configuration Debug -Platform x64
```

产物为 `FatFish/x64/Debug/FatFishCli.exe`（Win32 产物位于 `FatFish/Debug`）。支持 Debug/Release × Win32/x64；Debug 启用内存泄漏检查。

```powershell
# 从仓库根目录启动；env 和 memory 的定位使用可执行文件路径，不依赖工作目录。
& ./FatFish/x64/Debug/FatFishCli.exe
& ./FatFish/x64/Debug/FatFishCli.exe --once
```

`--once` 执行一轮实际截屏和模型请求后退出。也可用 `--repo-root PATH` 指定包含 `env` 的目录，或 `--help` 查看参数。交互按键在当前一轮结束后处理；网络或配置错误会终止测试程序以暴露问题。

离线测试由独立的 `UnitTest` 项目运行，测试源码、夹具和项目文件统一位于 `FatFish/UnitTest`，所有测试 PowerShell 脚本也直接放在此目录，使用 GacUI 的 Vlpp 单元测试框架，不再通过 CLI 的 `--self-test` 参数运行。测试使用临时记忆目录和模拟模型，验证文件安全边界、配置解析、流式工具调用拼接、输出格式、错误反馈及多轮代理流程，不读取真实密钥、不截屏、不联网。构建后，从仓库根目录运行：

```powershell
Set-Location FatFish
& "$PWD/../Release/.github/Scripts/copilotExecute.ps1" -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64
```

验证必须包含完整的 `UnitTest` 测试通过且无内存泄漏，以及同一个 `FatFishCli` 进程中使用真实模型连续完成 10 轮 ENTER，最后按 ESC 正常退出。修改项目配置时，还需构建并运行 Debug/Release × Win32/x64 的 `UnitTest`。

仅修改主题资源或文档、没有修改代码或项目配置时，只需检查变更的资源与元数据，无需构建、运行 `UnitTest` 或真实模型 CLI 验证。

构建后，在仓库根目录的 PowerShell 7 中运行 `& "$PWD/FatFish/UnitTest/Invoke.ps1" -Configuration Debug -Platform x64` 可验证实际截图、PNG 编码、HTTP 请求和记忆写入。入口脚本 `Invoke.ps1` 与本机回环测试服务器 `Server.ps1` 均直接位于 `FatFish/UnitTest`。此测试将截图仅发送到本机回环测试服务器，在内存中解码，不保存图片、不连接真实模型服务；结束后还原调试参数并删除临时目录。

`Agents` 静态库包含全部代理、工具、配置和截图逻辑；CLI 负责参数、按键、输出和目录定位。CLI 先读取自身可执行文件的完整路径，再用 `vl::filesystem::FilePath` 计算 `env` 和 `memory`，作为两个独立路径传给 `FairyApplication`；`Agents` 不查找仓库根目录，也不假设两个目录的名称或相对位置。按当前构建布局，x64 从可执行文件所在目录使用 `../../../env` 和 `../../../memory`，Win32 使用 `../../env` 和 `../../memory`，Debug 与 Release 相同。`--repo-root` 在 CLI 中覆盖根目录，未来 `FatFishFairy` 也遵循同样的目录传入设计。每轮包含全部显示器的 PNG 图像、坐标和尺寸，支持负坐标与混合 DPI。图像只在内存中处理并发送给配置的模型服务器；精灵只接收视觉描述。`env` 的中文工具说明、记忆指引和角色请求随每次模型提交发送，精灵额外接收原有的 `Character.md`。

CLI 在每条回复接收完整后，将双方的 `speak` 调用显示为下面的文本块，保留原文换行和引号；其他工具调用、普通文字和空的结束消息仍以 `Vision> JSON` 或 `Fairy> JSON` 显示。混合工具调用保留顺序，已显示的发言不再重复出现在 JSON 中；无法解析的 `speak` 参数保留为 JSON 以便诊断。不打印发送给代理的请求。

```text
Vision (speak)>
****************
屏幕观察内容
****************
```

每个屏幕观察请求的提示要求视觉代理和精灵代理各恰好调用一次 `speak`，包括工具反馈后的后续交互；视觉代理提交完整观察，精灵没有想说的话时传递空的 `text`。为兼容模型不稳定的回复，运行时仍按执行顺序将同一代理本轮所有非空 `speak` 内容以换行连接，覆盖同一回复中的多次调用和跨回复的追加调用，不去重；空内容不增加分隔行。视觉代理的完整拼接结果传给精灵，精灵的完整拼接结果作为本轮返回值，每轮分别重新累积。普通 assistant 文字只出现在调试记录中，不代替视觉观察或精灵发言。工具参数解析或执行失败返回简短的工具错误；整个回复格式损坏时，以简短错误消息请求重新提交，不将损坏的调用加入历史或执行其中的工具。反馈不重复预定义提示，但请求仍携带原有会话上下文。每个代理每轮最多 24 次模型请求，防止无穷重试。

模型请求使用 `stream:true`，按 SSE 事件和工具索引拼接响应，保留首个片段的调用 ID、类型、函数名并连接后续参数。只有完整结束的响应才执行工具；也兼容忽略流式选项而返回完整 JSON 的服务器。某些兼容服务器的非流式响应会丢失函数名或调用 ID，无法可靠执行工具，流式响应能避开该问题。

记忆以 `Dictionary<WString, Ptr<List<WString>>>` 加载，路径统一为相对路径，读和搜索使用内存；写入同步到 UTF-8 文件。文件工具限制在 `memory`，禁止路径穿越、Windows 设备路径和链接，保护 `memory/Index.md`；该目录不提交 Git。重启会重新加载磁盘记忆，但精灵的完整对话只在本次进程中保留。工具定义见 [env/Tools.md](env/Tools.md)。

HTTP/HTTPS 网页 GET 使用 Vlpp 的 `HttpClientApi`。带认证的模型 POST 使用 WinHTTP，因为现有封装没有禁止转发自定义认证头的重定向选项。模型请求不跟随重定向；应配置最终服务地址。全显示器捕获使用 Windows GDI/WIC，无需启动 GacUI 窗口。协议参考 [Chat Completions API](https://developers.openai.com/api/reference/resources/chat)。

`FatFishFairy` 项目目前是预留项目，桌面精灵窗口不在本阶段范围内。

主题图片保存在 `themes`：`theme.json` 将文件夹名映射到中文主题名，每个主题的 `index.json` 将动画名映射到独立帧数（不包含播放重复次数），`reference.png` 保留原始角色参考图。更新任务见 [themes/job.updateThemes.prompt.md](themes/job.updateThemes.prompt.md)，其中 `xN` 指定对应阶段的帧数。现有 `loli_maid`（萝莉小妹抖）包含 10 组动画，共 34 帧，文件按 `<动画名>_1.png` 起连续编号。

| 动画 | 帧数 | 内容 |
| --- | --- | --- |
| `coffee` | 3 | 跪坐在矮桌旁捧杯、轻啜手冲咖啡、闭眼微笑 |
| `espresso` | 5 | 布粉、压粉两帧，再用 Decent Espresso 咖啡机萃取、打奶泡、倒奶三帧 |
| `latte_art` | 3 | 倒奶、形成爱心、完成拿铁拉花 |
| `homework` | 3 | 做作业、思考、继续书写 |
| `reading_manga` | 3 | 躺在沙发上读漫画、翻页、开心地笑 |
| `sleeping` | 3 | 趴在地上睡觉，伴随轻微呼吸与尾巴变化 |
| `playing` | 3 | 坐在地上握住、举起和玩耍玩具飞机 |
| `programming` | 3 | 坐人体工学椅在书桌前编程、思考、继续输入 |
| `drawing` | 3 | 用电子画板绘制自己的草稿、描线、上色 |
| `transformer` | 5 | 从女仆经过机械折叠、半鲸形、机械鲸鱼变为蓝色鲸鱼 |

每个动画由 3–5 张 384×384 RGBA PNG 组成。角色与道具共用一片连通的白色贴纸底，周围保留约 8 像素白边，最外层为 1 像素 `#E0E0E0` 浅灰边，外部完全透明；同一动画保持构图尺寸与基准位置一致，普通动作共用稳定的外轮廓，变形动画的轮廓随形体变化。文字使用蓝色中文，数字、颜文字和 `Zzz` 等符号除外。后续桌面窗口应以每秒一帧播放当前主题中随机选出的动画系列，按帧顺序连续完整播放3遍（总共3遍），第3遍最后一帧结束后才随机选择下一个系列，并在每次启动时使用新的随机种子；当前仅提供主题资源，尚未实现播放器。
