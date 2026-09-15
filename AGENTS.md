# FatFishFairy

<!-- All comments in this format should be kept -->

The goal of this project is to create a desktop fairy with realtime response to user actions:
- The app keeps taking snapshots of all monitors in a reasonable rate.
- A model with stateless session (the vision agent) describe what could be seen from snapshots, another model with memory (the fairy agent) react to the description.
- The fairy tries to remember any interesting stuff about the user.
- To simplify the architecture, everything could be running in the same thread.

This application is Windows only, no need to worry about cross platform stuff.

Note that agent expects a OpenAI v1 chat completion protocol, it could use any official or self-hosted LLM that supports it.

## Maintenance

At the end of any coding task, update AGENTS.md and README.md to fix stale fact or add important information, commit and push local changes to the main branch, rebase if conflict.

DO NOT maintain README.md as the Chinese translation of AGENTS.md. Instead it should only has the following topic:
- How to prepare, build and test this repo.
- How to use `FatFishCli` and `FatFishFairy` and what do they do from user's perspective.
Maintain README_EN.md as the English translation of README.md and leave the `中文 | English` link in both of them for easy switching.

If the source code is not touched in a request, you are not required to run the verification.
If `Agents` folder is changed, you need to run full verification as well as `FatFishFairy`, otherwise only run verification on test apps that is affected in:
- `UnitTest`
- `FatFishCli`
- `FatFishFairy`

## Supported Tools

- Tools available to all agents
  - HTTP/HTTPS querying, for learning knowledges.
  - File reading.
  - File writing, unexisting folders and the target file will be created recursively.
  - File deleting, folders without any file will be deleted:
    - From the direct folder of the target file to the root folder, if a folder is empty, delete that folder.
    - The `ROOT-REPO/memory` folder should never be deleted.
    - The `ROOT-REPO/Index.md` file should never be deleted.
  - File searching.
  - Speak:
    - For the vision agent: description from snapshots.
    - For the fairy agent: anything want to say to the user.

### Restrictions

- File access should only limit to everything in the`REPO-ROOT/memory` folder.
- Path given to file tools should not contain anything like `.` or `..` that could escape from the folder. Later the path should be first expanded to an absolute path, verify and deny if the target file is not in the memory folder.
- All files will be loaded into a `Dictionary<WString, Ptr<List<WString>>>` data structure:
  - The key is a normalized path to the memory folder.
  - The value is all lines of the file.
  - Reading and searching could be performed without accessing the file system, writing will be submitted to the dictionary and the file system.

### Specification

Maintain tool spec in `REPO-ROOT/env/Tools.md` in this format:
```markdown
## Specification (TOOL-NAME)
Request and response format, behavior, other details
```

## Authorization

`REPO-ROOT/env/apikey.json` has anything needed for the authorization. This file contains sensitive information:
- It cannot be committed to git, always make sure `REPO-ROOT/env/.gitignore` already excludes it.
- All agents should use its own dedicated model.
- Follow OpenAI chat competion protocol but it connects to a non-official server.

## Agents

In `REPO-ROOT/env` these files are submitted to agents accordingly, in each request submission:
- `Tools.md`, describe specification of all tools to all agents.
- `Guidance.md`, guidance about how to maintain memories based on the file system, including that `Index.md` should be used to index all other files, offering efficient advices.
- `Character.md`, fixed request to the fairy agent, about its characteristic.
- `Request_Vision.md`, fixed request to the vision agent.
- `Request_Fairy.md`, fixed request to the fairy agent.

**IMPORTANT**: All files listed here should be in Chinese. Except `Character.md`, all files could be modified during development.

### the Vision

- Requests to the agent should combine these prompt files in `REPO-ROOT/env` in this order:
  - `Tools.md`.
  - `Guidance.md`
  - `Request_Vision.md`
  - Embed snapshots of all monitors.
- Expect very detailed description from the snapshot.
- Every round starts a new session, nothing from the last round is needed.

### The Fairy

- Requests to the agent should combine these prompt files in `REPO-ROOT/env` in this order:
  - `Tools.md`
  - `Guidance.md`
  - `Request_Fairy.md`
  - `Character.md`
  - `当前日期时间是：YYYY-MM-DD HH-mm-ss`
  - `以下是用户所有屏幕的内容：` + Description from the snapshots.
- Use local system time with zero-padded fields and a 24-hour clock. Read it once after the vision agent finishes each round, and store the timestamp and labeled complete observation together in that round's user message. Preserve the original timestamps in conversation history and tool-feedback follow-ups.
- The agent will access and maintain memories about anything, especially any interesting stuff about the user, try to summarize and infer what the user like, what the user is usually doing, etc.
- The agent may choose to say something to the user.
- Every round runs in the same session.

## File Organization

- `env`: files to be loaded.
- `memory`: the folder for agents to maintain their memory.
- `themes`: desktop fairy artwork; `theme.json` maps theme folder names to Chinese display names, and each theme's `index.json` maps animation names to frame counts. Keep the supplied `reference.png` as the character reference.
- `Release`: the submodule to `https://vczh-libraries/Release`.
- `Agents`: shared feature source files only; no test cases, fixtures, or test runners.
- `FatFish`:
  - `FatFish.sln`: The solution file.
  - `Agents/Agents.vcxproj`: A shared library to index all `Agents` source files.
  - `FatFishCli/FatFishCli.vcxproj`.
  - `FatFishFairy/FatFishFairy.vcxproj`.
  - `UnitTest`: all test cases and fixtures. Keep the opt-in platform integration scripts `Invoke.ps1` and `Server.ps1` directly in this folder, alongside the test sources and project files.
  - `UnitTest/UnitTest.vcxproj`: A dedicated console test executable referencing `Agents` and using GacUI's Vlpp unit test framework.

## Important vcxproj Settings

- `Windows SDK Version`: `Windows 10.0 SDK (latest)`
- `Platform Toolset`: `v145 for Microsoft C++ Build Tools`
- `C++ Language Standard`: `ISO C++ 20 Standard`
- `Include Directories`: add the `REPO-ROOT/Release/Import` folder directly.
- `Use Library Dependency Inputs`: `Yes`
- `Preprocessor`:
  - `VCZH_DEBUG_NO_REFLECTION`: all reflection code will be eliminated during compiling
  - debug profile
    - `VCZH_CHECK_MEMORY_LEAKS`
    - memory leaks checking should only be applied on debug profile.
- You can copy settings from the `Release` submodule as a start.

## Building

- The build helper requires `VLPP_VSDEVCMD_PATH` to point to the installed Visual Studio / Build Tools `VsDevCmd.bat`. Run it from the folder containing the target solution (`FatFish` for this app, `Release/Tools/Executables` when bootstrapping resource tools).
- There are already powershell scripts from the `Release` submodule to build and debug any vcxproj project, you are recommended to use them.
- If they must be modified, copy them to this repo, do not update `Release` for such reasons.

## Shared Library

### GacUI

- Maintain a submodule to `https://vczh-libraries/Release` to the `Release` folder.
- Always update the submodule to its latest `master` branch before working.
- It contains all necessary C++ constructions, you are strong recommended to use them instead of STL, Windows API, etc.
  - Windows API is allowed if GacUI doesn't offer direct solution.
- `HttpClientApi` encapsulates the [WinHTTP](https://learn.microsoft.com/en-us/windows/win32/winhttp/about-winhttp) api for easier use.
  - If it lacks of `https` ability, you can add it.
- From its `.github/copilot-instructions.md` it has everyting you need to understand the library.

#### Updating GacUI

You are not recommended to modify this library, but if you really need to:
- The whole organization is cloned in `REPO-ROOT/../../vczh-librarires`, read its `AGENTS.md` before starting.
- You should commit and push all local changes in that organization, and then update the `Release` submodule, to use your fix.
- If you find yourself do not as the permission to submit to this github organization, then just don't change it.

### Agents

- `FatFishCli`, `FatFishFairy` or any other interactive test apps should only be a thin UI layer. `Agents/Desktop.h` contains desktop position persistence, theme metadata loading and animation sequencing; the GUI owns native windows, image decoding and rendering.
- `FatFishCli` and `FatFishFairy` own the locations of `env` and `memory`. Each app must first obtain its own executable's full path, then use `vl::filesystem::FilePath`, `GetFolder()` and `/` to calculate both folders and pass them as two separate `vl::filesystem::FilePath` arguments to `FairyApplication`.
- Match `FatFish/Common.props`: executables are in `REPO-ROOT/FatFish/x64/<Configuration>` for x64 and `REPO-ROOT/FatFish/<Configuration>` for Win32. From `vl::filesystem::FilePath(executable).GetFolder()`, use `L"../../../env"` and `L"../../../memory"` for x64, or `L"../../env"` and `L"../../memory"` for Win32, in both Debug and Release. Update this calculation in both apps if the output layout changes.
- Default folder resolution must depend on the executable location, not the working directory or an upward search for marker files. Any explicit path override (such as CLI `--repo-root PATH`) is also resolved by the UI before passing the two folders to `Agents`.
- Until the desktop GUI executes agents, it only needs to resolve `env` and `themes` using the same executable-relative root calculation as the CLI. It must not instantiate `FairyApplication` or require `apikey.json` merely to display the fairy.
- Code in `Agents` must not discover or store the repository root, inspect the executable path, or assume the supplied folders' names, locations or relationship. Load configuration and prompts directly from the supplied environment folder and initialize `MemoryStore` with the exact supplied memory folder. The constructor that injects configuration, prompts and I/O for offline tests only needs the supplied memory folder.
- All source files about agents and other features should be in the `REPO-ROOT/Agents` folder.
- Keep test cases and fixtures in `REPO-ROOT/FatFish/UnitTest`, compiled only by the `UnitTest` project. Keep all test PowerShell scripts directly in this folder. Do not expose test runners from feature headers or add a `--self-test` mode to `FatFishCli`.
- Prompts must require both the vision agent and the fairy agent to call `speak` exactly once per observation request, including all tool-feedback follow-ups. Vision submits its complete nonempty observation; fairy uses an empty `text` when it has nothing to say.
- The runtime must tolerate extra `speak` calls from either agent: concatenate all successfully parsed nonempty texts in execution order with newlines, both within one response and across follow-ups. Do not discard repeated text or reject extra calls merely for exceeding the prompted count. Empty texts add no separator.
- Keep speech accumulation local to each agent's current round. Pass the full vision result to the fairy and return the full fairy result; ordinary assistant text is not part of either result.

## FatFishCli test app

The CLI window title should be `FatFishCli`.

Accept two keys:
- `ESC`: exit.
- `ENTER`: run the vision agent followed by the fairy agent. Keep other tool calls and ordinary responses as `Vision> JSON` or `Fairy> JSON`. Render each `speak` call from either agent as the block below, replacing `AGENT` with `Vision` or `Fairy` and `CONTENT` with its decoded text. Keep mixed tool calls in order without duplicating speech in JSON. Do not print requests sent to agents; the vision observation is passed to the fairy.

```text
AGENT (speak)>
****************
CONTENT
****************
```

## UnitTest and Verification

- Changes limited to theme assets or documentation only require checking the changed assets and metadata; the solution build, UnitTest suite and real-model CLI verification below are not required when no code or project configuration changes.
- Register offline tests with GacUI's Vlpp `TEST_FILE` and `TEST_CASE` macros, use framework assertions such as `TEST_ASSERT` and `TEST_EXCEPTION`, and run them through `vl::unittest::UnitTest::RunAndDisposeTests`. Finalize global storage and check for memory leaks in Debug builds.
- Verification must build the solution and run the complete `UnitTest` suite successfully, with no skipped test files or memory leaks. When adding or changing project configurations, build and run `UnitTest` for Debug/Release × Win32/x64.
- From `REPO-ROOT/FatFish`, run `& "$PWD/../Release/.github/Scripts/copilotBuild.ps1" -Configuration Debug -Platform x64`, followed by `& "$PWD/../Release/.github/Scripts/copilotExecute.ps1" -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`. Use the corresponding configuration and platform for the other builds.
- Offline `UnitTest` verification must use synthetic model responses and temporary directories without reading real credentials, capturing the desktop, or making network requests. Cover memory safety, configuration validation, completion streaming, response formatting, error feedback, and multi-round agent execution.
- Offline verification must cover multiple `speak` calls within one response and across follow-ups for both agents, complete vision-to-fairy forwarding, per-round result isolation, and an empty fairy `speak`.
- For platform integration verification, run `REPO-ROOT/FatFish/UnitTest/Invoke.ps1` in PowerShell 7 after building `FatFishCli`. This opt-in test captures the desktop and uses only a local loopback fixture implemented by `Server.ps1` in the same folder.
- For desktop-window integration, run `FatFish/UnitTest/Invoke-Fairy.ps1` in PowerShell 7. It uses a temporary executable/theme/config layout, verifies transparency, animation, the startup balloon and its movement, dragging, persistence and menu exit, and never copies credentials. It moves the mouse during the test and restores the pointer afterwards.
- Verification must include 10 consecutive successful `ENTER` rounds in `FatFishCli`, using the configured real models in one running process.
- Each round must finish the vision agent followed by the fairy agent successfully. After all 10 rounds, press `ESC` and verify a clean exit.
- If any round fails, fix the problem and restart the 10-round verification before reporting completion.

## FatFishFairy

The application is based on GacUI, using `Release/Tools/GacBuild.ps1` to compile `FatFish/FatFishFairy/UI/Resource.xml` to `FatFish/FatFishFairy/UI/Source`, the `CppCompressed` options embed generated binary resources in `FatFishUIResource.cpp`. The application uses the ordinary Windows Direct2D renderer, without hosted mode. The project invokes GacBuild through the adjacent `UI/GacUI.xml` driver before compiling; bootstrap the ignored `GacGen.exe` and `CppMerge.exe` as described in `Release/Tools/README.md` if missing. Keep generated C++ files committed with the XML.

### Main Window

A frameless control template should be created for the main window.
The main window is 384x384 without border, this could be implemented with customized frame enabled.
The background should be #00FF00 so that when a png renders on it, the background color could be specified as a transparent color to make the transparent part in the png actually transparent.
`GetWindowsForm` could be used to get the `HWND` of the main window, with appropriate Windows API to implement the transparent feature.

### Behavior

The main window is always top-most.
On startup, show `Hello, world!` in a native Win32 tracking balloon tooltip (`TOOLTIPS_CLASSW`, `TTS_BALLOON`, `TTF_TRACK`) just above the window, with its stem pointing down at the window's top center. Keep it visible until the window closes and update its screen position from the GacUI `Moved()` callback, calling the base implementation first. Create and activate it when `WindowOpened` fires. Windows chooses stem direction automatically: use `TTM_TRACKPOSITION` at a point inside the related monitor's bottom edge to obtain its above-target layout, then measure the complete window with `GetWindowRect` and the stem's horizontal offset from that target. Cache the layout target to avoid moving the tooltip back to the monitor edge during ordinary dragging; use `SetWindowPos` to move the complete native shape above the fairy and clamp it to the monitor's work area. Use the first screen if the fairy is wholly off-screen. Do not use `TTF_ABSOLUTE`, which leaves the stem pointing upward when the whole balloon is placed above the fairy. Own and destroy its `HWND` in `FairyDesktopWindow`. The executable opts into common controls v6 for system visual styles and the current `TOOLINFO` layout. Do not use a polling timer for bubble positioning. The desktop integration test must inspect the balloon's native window region to verify its downward stem and target, as window bounds alone cannot detect a reversed pointer.
Dragging the main window using left button moves the window. Remember the local cursor position on left `mouseDown`; on `mouseMove`, move the current native bounds by the converted difference from that fixed anchor. GacUI handles capture automatically. Save the position on left `mouseUp`; do not initiate native caption dragging or manage capture manually.
Right click the main window shows a menu organized as below:
- `退出`: Exit the application.

Define the named `contextMenu` ToolstripMenu component and its exit action in `UI/Resource.xml`. C++ only opens that generated component in response to right-click.

`REPO-ROOT/env/config.json` looks like this
```JSON
{
  "windowX": 0,
  "windowY": 0
}
```
This file defines the initial location of the main window. When stopping dragging the main window, this file should be updated to reflect the current location, therefore it is remembered and used at the next startup.
`config.json` is ignored by Git. Missing files or missing coordinates default to zero; dragging creates the file and preserves unrelated settings. Coordinates are signed 32-bit desktop coordinates, including negative positions on other monitors. Malformed configuration and missing or invalid theme frames fail explicitly.

### Executing Agents

(non goal for now)

### Playing Animation

Theme assets are maintained separately from the desktop window implementation. Follow `themes/job.updateThemes.prompt.md` and mark an animation complete only after its files and metadata are verified. `themes/loli_maid` currently contains 10 animation series and 34 frames: `coffee`, `espresso`, `latte_art`, `homework`, `reading_manga`, `sleeping`, `playing`, `programming`, `drawing`, and `transformer`. `espresso` has five frames (two puck-preparation frames followed by three latte-making frames), and `transformer` has five transformation stages; all other series have three frames. Respect explicit `xN` stage counts in the prompt. Frames use contiguous `<animation>_1.png` names, are 384×384 RGBA PNGs, and have a single connected white sticker backing with about 8 pixels of padding, a one-pixel `#E0E0E0` outer edge, and fully transparent exterior. Keep scale and placement consistent across an animation; use a stable outline for ordinary actions and let transformation outlines follow the changing body shape. Preserve existing character references and completed animations when adding series. `index.json` stores distinct frame counts, not playback repetition counts.

For `reading_manga`, the viewer sees the book's outer covers; a turning interior page rises behind those covers and stays attached to the binding. For `playing`, the character kneels with smooth knees in front and feet folded behind under the skirt. For `programming`, the viewer sees the monitor's rear casing with blue `C++` lettering, never screen contents. Preserve these corrections when updating later frames.

The desktop player uses the first theme in `themes/theme.json`, selects an animation series randomly within that theme, shows one frame per second, and plays its complete frame sequence three consecutive times in total. It selects the next series randomly only after the last frame of the third playthrough, and seeds its random generator afresh on each process start. `index.json` defines the distinct frame count. Playback is implemented by `Agents/Desktop.cpp` and the GUI timer, separately from the asset update job.

`FairyDesktopWindow` implements `INativeControllerListener::GlobalTimer` and updates the image only after at least 1000 monotonic milliseconds. Register the listener after initializing the window and unregister it in the derived destructor. GacUI already runs the global timer; do not start a separate timer or create an `IGuiAnimation` for frame playback.

## Important Learning

<!--
You can write anything in this section during development to make future works more efficient.
-->

- Current GacUI composition input uses `mouseDown`/`mouseUp` plus `GuiMouseEventArgs::button`, and `mouseMove` plus `arguments.left`. Event coordinates are GUI units; convert movement deltas with `INativeWindow::Convert` before updating native bounds. Keep the mouse-down anchor unchanged because moving the window updates the cursor's relative position.
- Use an adjacent `GacUI.xml` driver for GacBuild. Passing the resource itself as its driver makes its resource compiler replace the same `.log` folder that contains GacBuild's enumeration files.
