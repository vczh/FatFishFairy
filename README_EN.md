# FatFishFairy

[中文](README.md) | [English](README_EN.md)

A desktop fairy for Windows, with two applications:

- **FatFishFairy**: displays an always-on-top transparent character window, plays animations, and supports dragging with position persistence. Model integration is not yet available, so no API key is required.
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

### Configure models (FatFishCli only)

Before using the CLI for the first time, create a local configuration from the template. If you already have one, edit it directly:

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

Git ignores `env/apikey.json`; do not commit API keys. The desktop application and offline unit tests do not need this configuration.

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
- Hold the left mouse button on the character to drag it. Releasing the button saves its position, which is restored on the next launch.
- Right-click to open the menu and select “退出” (Exit) to close the application.
- Uses the first theme in `themes/theme.json`, picks an animation at random, and plays its complete sequence three times at one frame per second before randomly choosing the next animation.

The included theme has 10 animations with 34 frames, featuring coffee, homework, manga, sleeping, playing, programming, drawing, and transformation.

The window position is saved in `env/config.json`, which normally needs no manual setup. You can also edit it while the application is closed:

```json
{
  "windowX": 0,
  "windowY": 0
}
```

A missing file or coordinate defaults to zero. Negative coordinates on other monitors are supported. Dragging creates or updates the file while preserving other fields; Git ignores this file. Invalid configuration, invalid coordinates, or missing or invalid theme images cause an error.

The desktop window currently only plays animations; it does not automatically capture the screen or call models. Use the CLI below for screen observation and model responses.

## Use FatFishCli

After configuring the models, run it in a terminal:

```powershell
& ./FatFish/x64/Debug/FatFishCli.exe
```

- **ENTER**: captures all monitors, runs the vision model, and passes the complete observation to the fairy model.
- **ESC**: exits. During a round, keypresses are handled after that round finishes.

Each ENTER keypress runs one round; there is no automatic continuous capture. Screenshots are processed in memory and sent to the configured vision model service; the fairy model receives the observation text. The vision model starts a new session each round. The fairy model retains its conversation within the same process, can maintain files in `memory`, and may choose to remain silent.

Other launch options:

```powershell
# Capture the screen and run one model round, then exit
& ./FatFish/x64/Debug/FatFishCli.exe --once

# Use env and memory under another directory
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

Tests use synthetic model responses and temporary directories without reading real credentials, capturing the screen, or accessing the network. They cover memory file safety, configuration, streaming responses, output formatting, error feedback, multiple model rounds, window position, and animation sequencing. The complete suite should pass, with no memory leaks in Debug builds.

### Local integration tests

After building the corresponding applications, run these in PowerShell 7:

```powershell
# CLI: real screen capture, PNG encoding, local HTTP requests, and memory writes
& ./FatFish/UnitTest/Invoke.ps1 -Configuration Debug -Platform x64

# Desktop window: transparency, topmost behavior, animation, dragging, position restore, and menu exit
& ./FatFish/UnitTest/Invoke-Fairy.ps1 -Configuration Debug -Platform x64
```

The CLI integration test sends screenshots only to a local loopback test server. It does not save images or access a real model service. The desktop integration test uses a temporary application, themes, and configuration without reading real configuration or credentials. It moves the mouse during the test and restores the pointer afterward.

### Real-model verification

After configuring the models, start one interactive `FatFishCli` process and complete **10 consecutive ENTER rounds**, waiting for both models to finish successfully each time. Then press **ESC** and confirm a clean exit. If any round fails, fix the problem and restart the 10-round verification. This verification captures the screen and contacts your configured real model service.

Changes to shared `Agents` code require full verification, including the desktop integration test; changes to one application require verification of the affected application. Project configuration changes require building and running unit tests for `Debug` / `Release` × `Win32` / `x64`. If only documentation or theme assets change, with no code or project configuration changes, inspect the changed content; builds and the verification above are not required.
