# FatFishFairy

[中文](README.md) | [English](README_EN.md)

A desktop fairy for Windows, with two applications:

- **FatFishFairy**: displays an always-on-top transparent character window, continuously observes the screen, maintains memories, and responds in a talking bubble. It plays animations and supports theme switching and dragging while remembering the selected theme and position.
- **FatFishCli**: observes the screen on a keypress. A vision model describes screenshots from all monitors, then a fairy model uses that description to maintain memories and respond in the terminal.

## Preparation

### Development environment

- Windows.
- Git and PowerShell 7.
- Visual Studio or Microsoft C++ Build Tools with the desktop C++ development components, the **v145 toolset**, and the latest Windows 10 SDK. The projects use C++20.

Clone the repository and its GacUI dependency:

```powershell
git clone --recurse-submodules https://github.com/vczh/FatFishFairy.git
Set-Location FatFishFairy
```

For an existing checkout, run these commands from the repository root to initialize dependencies and update `Release` to its latest `master`:

```powershell
git submodule update --init --recursive
git submodule update --remote Release
```

Run all commands below in PowerShell 7 from the repository root. The build script requires `VLPP_VSDEVCMD_PATH`; replace the example path with the path to `VsDevCmd.bat` in your Visual Studio / Build Tools installation:

```powershell
$env:VLPP_VSDEVCMD_PATH = 'C:\path\to\Visual Studio\Common7\Tools\VsDevCmd.bat'
```

This setting applies only to the current PowerShell session.

### Prepare the resource compiler tools

Before the first build, if `Release/Tools/GacGen.exe` or `Release/Tools/CppMerge.exe` is missing, build and copy them:

```powershell
Push-Location Release/Tools/Executables
& "$PWD/../../.github/Scripts/copilotBuild.ps1" -Configuration Release -Platform x64
Copy-Item ./x64/Release/GacGen.exe, ./x64/Release/CppMerge.exe ../
Pop-Location
```

The desktop project automatically invokes these tools to generate UI resources during its build. See [Release/Tools/README.md](Release/Tools/README.md) for other ways to build the tools.

### Configure models

Before using FatFishFairy or FatFishCli for the first time, create a local configuration from the template. If you already have one, edit it directly:

```powershell
Copy-Item env/apikey-template.json env/apikey.json
```

Edit `env/apikey.json`:

| Field | Value |
| --- | --- |
| `apikey` | Your model service's API key. |
| `url` | An API base URL such as `https://your-server/v1`, or the full `/chat/completions` URL. Do not use a `/models` URL; use the final service URL that does not require a redirect. |
| `auth_header` | An authentication header such as `Authorization: Bearer $APIKEY`; `$APIKEY` is replaced with the key above. |
| `vision_model` | The model ID for screen observation. It must support image input and tool calls. |
| `fairy_model` | The model ID for fairy responses and memories. It must support tool calls. |

The service must support the OpenAI v1 Chat Completions protocol. Configure each role separately; both may use the same model ID. The legacy `chat_model` field remains an alias for `fairy_model`; if both are present, their values must match.

Git ignores `env/apikey.json`; do not commit API keys. Offline unit tests do not need this configuration, and local integration tests use separate fixture configuration.

## Build

After preparation, open `FatFish/FatFish.sln` in Visual Studio and build the solution, or run:

```powershell
Push-Location FatFish
& "$PWD/../Release/.github/Scripts/copilotBuild.ps1" -Configuration Debug -Platform x64
Pop-Location
```

All four combinations of `Debug` / `Release` and `Win32` / `x64` are supported; change the command parameters accordingly. Debug builds enable memory leak checks.

| Platform | Output directory |
| --- | --- |
| x64 | `FatFish/x64/<Configuration>/` |
| Win32 | `FatFish/<Configuration>/` |

The directory contains `FatFishFairy.exe`, `FatFishCli.exe`, and `UnitTest.exe`. The launch examples below use Debug x64 builds. Keep the repository's directory layout: applications locate configuration, memories, or themes relative to their executable, independently of the working directory.

## Use FatFishFairy

```powershell
& ./FatFish/x64/Debug/FatFishFairy.exe
```

- Displays a 384×384 transparent, frameless character window that stays on top.
- Shows a system balloon saying “Hello, world!” above the window at startup, then automatically captures all monitors, runs the vision model followed by the fairy model, replaces the bubble text with the fairy's speech, and immediately starts the next round.
- The bubble's pointer faces down toward the window's top center and follows the window; its placement adjusts near screen edges to keep it visible. The latest speech stays visible until the next update. When the fairy chooses to remain silent, the bubble clears and hides until there is new text.
- Each round's complete nonempty speech is appended to `env/history.md`, creating it when needed. Each entry begins with `# Speak YYYY-MM-DD HH-mm-ss`, followed by a blank line and the speech, using local time and UTF-8 encoding. Appending continues after a restart. Git ignores this file; startup greetings, silent results, and errors are excluded. Write failures appear in the bubble and trigger a retry. This log is specific to FatFishFairy.
- Model configuration or request failures appear in the bubble with the prefix “调用大模型发生错误：” (An error occurred while calling the model), followed by an automatic retry after one second. Correcting missing or invalid model configuration allows initialization to retry.
- Hold the left mouse button on the character to drag it. Releasing the button saves its position, which is restored on the next launch.
- Right-click to open the menu, switch character themes in the “主题” (Theme) submenu, or select “退出” (Exit) to close the application. Themes appear by their Chinese display names in `themes/theme.json` order, with a check beside the current theme.
- Switching themes immediately picks an animation from the new theme and starts at its first frame, then saves the choice for the next launch. If no theme has been saved or the saved theme no longer exists, the first theme in the list is used.
- The fairy's personality comes from `themes/<current-theme>/Character.md`, falling back to `themes/loli_maid/Character.md` when that file is absent. The current theme's personality is read before each fairy request. Switching themes preserves the conversation and memories; a request already in progress may still speak with the previous personality.
- Picks an animation at random from the current theme and plays its complete sequence three times at one frame per second before randomly choosing the next animation.

Two included themes provide 20 animations with 70 frames in total. Switch between them through the right-click menu:

- **萝莉小妹抖** (Chibi Maid): a chibi character with 10 animations and 34 frames, featuring coffee, homework, manga, sleeping, playing, programming, drawing, and transformation into a whale.
- **长大的妹抖** (Grown Maid): a character with normal adult proportions, with 10 animations and 36 frames, featuring coffee, phone browsing, sleeping, programming, drawing, teaching, and transformations into a whale-armored warrior and a whale sailor outfit.

Chibi Maid uses the original food-loving, work-shy, teasing tsundere personality in `themes/loli_maid/Character.md`. Grown Maid uses the independent personality in `themes/grown_maid/Character.md`: a knowledgeable, gentle and thoughtful maid with an older-sister manner who often shows concern and encouragement, with a slightly exaggerated anime-style delivery. She refers to herself only as “我” (I) and addresses the user only as “主人” (Master), without inventing nicknames. Select “主题 → 长大的妹抖” (Theme → Grown Maid) in the right-click menu to use it.

`env/Character.md` is no longer used. An existing personality file that is empty or unreadable causes an error; the desktop application retries after the file is corrected.

The window position and selected theme are saved in `env/config.json`, which normally needs no manual setup. You can also edit it while the application is closed:

```json
{
  "windowX": 0,
  "windowY": 0,
  "selectedTheme": "loli_maid"
}
```

A missing file or coordinate defaults the coordinate to zero. Negative coordinates on other monitors are supported. `selectedTheme` uses a theme folder key from `themes/theme.json` and must be a string; if it is missing or no key matches exactly, the first theme is used. Dragging and theme switching create or update the file while preserving each other's settings and other fields; Git ignores this file. Invalid configuration, invalid coordinates, an invalid theme selection type, or missing or invalid theme images cause an error.

The desktop application continuously captures the screen and contacts the configured model service. Screenshots are processed in memory and sent to the vision model; the fairy model receives the complete observation text and the local date and time when that observation finishes. The vision model starts a new session each round, while the fairy model retains its conversation within the process and can maintain files in `memory`. Dragging, theme switching, and exit remain available during model requests; exiting cancels pending requests and stops the loop.

## Use FatFishCli

After configuring the models, run it in a terminal:

```powershell
& ./FatFish/x64/Debug/FatFishCli.exe
```

- **ENTER**: captures all monitors, runs the vision model, and passes the complete observation to the fairy model.
- **ESC**: exits. During a round, keypresses are handled after that round finishes.

The CLI window title is `FatFishCli`. Each ENTER keypress runs one round; there is no automatic continuous capture. Screenshots are processed in memory and sent to the configured vision model service; the fairy model receives the observation text and the local date and time when that round's vision observation finishes (`YYYY-MM-DD HH-mm-ss`, using a 24-hour clock). The vision model starts a new session each round. The fairy model retains its conversation and each round's timestamp within the same process, can maintain files in `memory`, and may choose to remain silent.

The CLI always uses `themes/loli_maid/Character.md` for the fairy's personality, independently of the desktop application's saved theme selection.

Other launch options:

```powershell
# Capture the screen and run one model round, then exit
& ./FatFish/x64/Debug/FatFishCli.exe --once

# Use env, memory, and themes under another directory
& ./FatFish/x64/Debug/FatFishCli.exe --repo-root 'C:\path\to\FatFishFairy'

# Show options
& ./FatFish/x64/Debug/FatFishCli.exe --help
```

Vision observations and fairy speech appear in `Vision (speak)>` or `Fairy (speak)>` text blocks, for example:

```text
Vision (speak)>
****************
Screen observation text
****************
```

Other tool calls and ordinary responses appear as `Vision> JSON` or `Fairy> JSON` for diagnostics. Network or configuration errors terminate the CLI.

Memory files are saved in `memory` and reloaded after a restart; the full conversation lasts only for the current process. The models' file tools can access only the memory directory, which Git ignores. See [env/Tools.md](env/Tools.md) for tool descriptions.

## Test

### Offline unit tests

After building, run the full test suite:

```powershell
Push-Location FatFish
& "$PWD/../Release/.github/Scripts/copilotExecute.ps1" -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64
Pop-Location
```

Tests use synthetic model responses and temporary directories without reading real credentials, capturing the screen, or accessing the network. They cover memory file safety, configuration, streaming responses, output formatting, error feedback, multiple model rounds, window position, theme order, theme selection and fallback, settings persistence, animation sequencing, and speech history appending, timestamps, and write failures. The complete suite should pass, with no memory leaks in Debug builds.

### Local integration tests

After building the corresponding applications, run these in PowerShell 7:

```powershell
# CLI: real screen capture, PNG encoding, local HTTP requests, and memory writes
& ./FatFish/UnitTest/Invoke.ps1 -Configuration Debug -Platform x64

# Desktop window: animation and interaction, continuous model calls, bubble updates, error recovery, and exit during requests
& ./FatFish/UnitTest/Invoke-Fairy.ps1 -Configuration Debug -Platform x64
```

Both integration tests send monitor screenshots only to a local loopback test server. They do not save those screenshots or access a real model service. The desktop integration test uses temporary application, theme, configuration, and memory directories to check transparency, topmost behavior, animation, balloon tracking, dragging and position restore, theme menu order, saved switching, startup restoration, and fallback for unknown theme keys. It also checks continuous model calls, updated and silent speech, error display and recovery, and menu exit while a request is pending. Tests do not read real configuration or credentials. Add `-ScreenshotPath PATH` to save test window, menu, and bubble screenshots for visual checks of the Chinese theme names, selection mark, and complete speech. The actual on-screen capture of long speech ends in `.speech-desktop.png`. The test moves the mouse and restores the pointer afterward.

### Real-model verification

After configuring the models, start one interactive `FatFishCli` process and complete **10 consecutive ENTER rounds**, waiting for both models to finish successfully each time. Then press **ESC** and confirm a clean exit. If any round fails, fix the problem and restart the 10-round verification. This verification captures the screen and contacts your configured real model service.

Also start one `FatFishFairy` process using the configured real models and confirm that **at least two nonempty fairy speech results appear in its actual bubble within the first two minutes after startup**. Inspect each bubble update on screen; parsed `speak` calls, logs, or a passing local fixture test do not satisfy this requirement. Exit through the menu and confirm a clean shutdown. If the requirement is not met, fix the problem and restart the desktop verification.

The default personality avoids commenting on the fairy itself or repeating comments about an unchanged screen. During bubble verification, show unrelated desktop content that changes as you interact with it.

Changes to shared `Agents` code require full verification, including the desktop integration test; changes to one application require verification of the affected application. Project configuration changes require building and running unit tests for `Debug` / `Release` × `Win32` / `x64`. If only documentation or theme assets change, with no code or project configuration changes, inspect the changed content; builds and the verification above are not required.
