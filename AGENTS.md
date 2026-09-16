# FatFishFairy

<!-- All comments in this format should be kept -->

The goal of this project is to create a desktop fairy with realtime response to user actions:
- The app keeps taking snapshots of all monitors in a reasonable rate.
- A model with stateless session (the vision agent) describe what could be seen from snapshots, another model with memory (the fairy agent) react to the description.
- The fairy tries to remember any interesting stuff about the user.
- Run each vision-fairy round sequentially; the desktop app uses an owned background worker so model requests do not block the UI.

This application requires Windows 10 or later, no need to worry about cross platform stuff. The existing capture implementation uses per-monitor DPI awareness v2; Windows 7 is not supported.

Note that agent expects a OpenAI v1 chat completion protocol, it could use any official or self-hosted LLM that supports it.

## Maintenance

At the end of any coding task, update AGENTS.md and README.md to fix stale fact or add important information, commit and push local changes to the main branch, rebase if conflict.

DO NOT maintain README.md as the Chinese translation of AGENTS.md. Instead it should only has the following topic:
- How to prepare, build and test this repo.
- How to use `FatFishCli` and `FatFishFairy` and what do they do from user's perspective.
- README is not the specification of the software, focus more on what users can do and what users can get, instead of describing too much details.
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

In `REPO-ROOT/env` these shared prompt files are submitted to agents accordingly, in each request submission:
- `Tools.md`, describe specification of all tools to all agents.
- `Guidance.md`, guidance about how to maintain memories based on the file system, including that `Index.md` should be used to index all other files, offering efficient advices.
- `Request_Vision.md`, fixed request to the vision agent.
- `Request_Fairy.md`, fixed request to the fairy agent.

The fairy's character prompt is stored in `REPO-ROOT/themes/<theme>/Character.md`. The original prompt is preserved in `themes/loli_maid/Character.md`; `env/Character.md` is no longer used.

`themes/grown_maid/Character.md` provides the grown blue whale maid's independent Chinese personality: a knowledgeable, gentle and encouraging adult with a playful anime-style delivery. She refers to herself only as `我` and addresses the user only as `主人`, without nicknames, including after theme switches. Its source research and design rationale are in `themes/grown_maid/Character.research.md`, which is documentation and must not be loaded as a character prompt.

`themes/nurgling/Character.md` provides the Nurgling's independent Chinese personality: a gleefully simple, playful little prankster inspired by Warhammer 40,000, openly affectionate toward Nurgle as `慈父` or `纳垢爷爷`. Its four defining traits are easy delight, love of play, love of Grandfather Nurgle, and love of pranks; keep its affection direct rather than tsundere, sarcastic or concealed behind denials. It uses `我` (occasionally `小纳垢灵`) for itself and `你` or `园友` for the user, without carrying over either maid's identity after a theme switch. Its background research and the rationale for this cute desktop adaptation are in `themes/nurgling/Character.research.md`, which is documentation and must not be loaded as a character prompt.

**IMPORTANT**: All prompt files listed here should be in Chinese. Except `Character.md`, all files could be modified during development.

### the Vision

- Requests to the agent should combine these prompt files in `REPO-ROOT/env` in this order:
  - `Tools.md`.
  - `Guidance.md`
  - `Request_Vision.md`
  - Embed snapshots of all monitors.
- Expect very detailed description from the snapshot.
- Every round starts a new session, nothing from the last round is needed.

### The Fairy

- Requests to the agent should combine these prompts in this order:
  - `REPO-ROOT/env/Tools.md`
  - `REPO-ROOT/env/Guidance.md`
  - `REPO-ROOT/env/Request_Fairy.md`
  - `Character.md`
    - For `FatFishCli` always use `REPO-ROOT/themes/loli_maid/Character.md`.
    - For `FatFishFairy` use `REPO-ROOT/themes/<round-theme>/Character.md`, where `round-theme` is the theme selected when the round starts. If such file does not exist, use the one for `loli_maid`.
    - Read the character file before each fairy request, including tool-feedback follow-ups. The desktop keeps the same theme path throughout a round, while reading that file's latest contents. An existing but empty or unreadable character file is an error; fall back only when the selected theme's file is absent.
  - `当前日期时间是：YYYY-MM-DD HH-mm-ss`
  - `以下是用户所有屏幕的内容：` + Description from the snapshots.
- Use local system time with zero-padded fields and a 24-hour clock. Read it once after the vision agent finishes each round, and store the timestamp and labeled complete observation together in that round's user message. Preserve the original timestamps in conversation history and tool-feedback follow-ups.
- The agent will access and maintain memories about anything, especially any interesting stuff about the user, try to summarize and infer what the user like, what the user is usually doing, etc.
- The agent may choose to say something to the user.
- Reuse the fairy session across rounds until the desktop theme changes; the next round then starts a fresh fairy session. Both apps also trim or reset fairy conversation history when recovering from a context overflow, as described below.

## File Organization

- `env`: shared prompts and local model/window configuration.
- `memory`: the folder for agents to maintain their memory.
- `themes`: desktop fairy artwork and per-theme `Character.md` prompts; `theme.json` maps theme folder names to Chinese display names, and each theme's `index.json` maps animation names to frame counts. Keep the supplied `reference.png` as the character reference.
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

- `FatFishCli`, `FatFishFairy` or any other interactive test apps should only be a thin UI layer. `Agents/Desktop.h` contains desktop position and theme selection persistence, theme metadata loading and animation sequencing; the GUI owns native windows, image decoding and rendering.
- `FatFishCli` and `FatFishFairy` own the locations of `env` and `memory`. Each app must first obtain its own executable's full path, then use `vl::filesystem::FilePath`, `GetFolder()` and `/` to calculate both folders and pass them as two separate `vl::filesystem::FilePath` arguments to `FairyApplication`.
- Match `FatFish/Common.props`: executables are in `REPO-ROOT/FatFish/x64/<Configuration>` for x64 and `REPO-ROOT/FatFish/<Configuration>` for Win32. From `vl::filesystem::FilePath(executable).GetFolder()`, use `L"../../../env"` and `L"../../../memory"` for x64, or `L"../../env"` and `L"../../memory"` for Win32, in both Debug and Release. Update this calculation in both apps if the output layout changes.
- Default folder resolution must depend on the executable location, not the working directory or an upward search for marker files. Any explicit path override (such as CLI `--repo-root PATH`) is also resolved by the UI before passing the two folders to `Agents`.
- The desktop GUI resolves `env`, `memory` and `themes` using the same executable-relative root calculation as the CLI. Initialize its `FairyApplication` on the background worker after opening the window; configuration or model errors must appear in the talking bubble while the window remains usable.
- Both apps resolve character paths under their executable-relative `themes` folder (or CLI `--repo-root`). Pass an app-owned character provider to `FairyApplication`; the desktop worker snapshots the selected character path at the start of each round and uses that path for all its fairy requests. Shared code may load the supplied character paths but must not derive the themes folder from `env` or `memory`.
- Code in `Agents` must not discover or store the repository root, inspect the executable path, or assume the supplied folders' names, locations or relationship. Load configuration and shared prompts directly from the supplied environment folder and initialize `MemoryStore` with the exact supplied memory folder. The constructor that injects configuration, prompts and I/O for offline tests only needs the supplied memory folder.
- All source files about agents and other features should be in the `REPO-ROOT/Agents` folder.
- Keep test cases and fixtures in `REPO-ROOT/FatFish/UnitTest`, compiled only by the `UnitTest` project. Keep all test PowerShell scripts directly in this folder. Do not expose test runners from feature headers or add a `--self-test` mode to `FatFishCli`.
- Prompts must require both the vision agent and the fairy agent to call `speak` exactly once per observation request, including all tool-feedback follow-ups. Vision submits its complete nonempty observation; fairy uses an empty `text` when it has nothing to say.
- Request `tool_choice: required` until the vision has submitted nonempty speech, or the fairy has submitted a valid `speak` (including empty text), then use `auto`. Keep requiring tools across other tool calls and invalid speech arguments so the fairy does not substitute ordinary assistant prose for its bubble output.
- The runtime must tolerate extra `speak` calls from either agent: concatenate all successfully parsed nonempty texts in execution order with newlines, both within one response and across follow-ups. Do not discard repeated text or reject extra calls merely for exceeding the prompted count. Empty texts add no separator.
- Keep speech accumulation local to each agent's current round. Pass the full vision result to the fairy and return the full fairy result; ordinary assistant text is not part of either result.

### Fairy context overflow recovery

- Apply this recovery only when the fairy model explicitly reports that the request exceeds its context limit. Preserve ordinary error behavior for the vision model, network failures, authorization failures and unrelated model errors.
- Track completed fairy rounds with explicit message boundaries. Remove whole oldest rounds, including their assistant messages and tool replies; do not estimate tokens, cut arbitrary messages or generate summaries.
- On the first overflow in a fairy round, snapshot the number `N` of completed historical rounds, remove the oldest `ceil(N / 3)` rounds, and retry the fairy request. On the second overflow in that same round, remove enough additional oldest rounds to reach `ceil(2 * N / 3)` removed rounds in total, using the original `N`. For `N = 0`, these two stages still retry with no historical rounds removed.
- These first two stages preserve the entire active round, its tool exchanges, accumulated speech and submitted-speech state. Resume the failed request without repeating completed tools or speech. Count overflows across all tool-feedback follow-ups within the same round.
- On the third overflow, reset the full fairy conversation, including the active assistant/tool exchanges and partial speech. Retry the fairy with only its system prompts and the exact original timestamped observation for this round. Renew the 24-step tool-completion budget for this fresh session while retaining the per-round overflow stage and three-retry limit. Require a new `speak`; do not retain abandoned partial speech in the completed round result. Completed memory-tool effects remain in place and are not rolled back.
- All three retries reuse the original complete observation and timestamp without recapturing the desktop or rerunning vision. A fourth overflow aborts the round with `FairyContextRecoveryExhausted`. The desktop displays the error and uses its existing one-second delay before starting a fresh vision-fairy round. Interactive CLI reports this error and waits for another `ENTER`; `--once` exits cleanly with a nonzero status. Other errors retain their existing behavior.
- Trimming or resetting conversation history never deletes memory files, undoes completed file writes or rewrites the desktop speech log. Reset the recovery stage and completed-round tracking when resetting the fairy session for a theme change.

## FatFishCli test app

The CLI window title should be `FatFishCli`.

The CLI uses the same desktop-session checks as the desktop app. On `ScreenCaptureUnavailable`, the interactive CLI reports that the desktop is unavailable and waits for another `ENTER` after unlocking or reconnecting, or `ESC` to exit. It does not automatically retry; `--once` exits cleanly with status 1.

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
- Offline context-overflow tests must cover structured and textual model errors, unrelated errors and vision exclusions, whole-round trimming with the original round-count thresholds (including zero and small histories), tool-message integrity, active speech and tool preservation during the first two retries, full reset on the third overflow, the retry limit, unchanged observation timestamps, persistent memory and recovery on a later round. The CLI loopback fixture must return a real HTTP 400 context-overflow body and verify the fairy retry retains its active messages without repeating tools or speech.
- Offline desktop tests must cover theme catalog order, exact-key selection and fallback, invalid selection types, and saving the selected theme without losing coordinates or unrelated configuration fields.
- Offline session tests must cover active/unlocked, active/locked, every non-active WTS connection state, and unknown states or query failures. Inject session checks to verify locking before capture, during capture, during model requests and between tools; discard the active observation and partial round while preserving completed history and completed memory effects, and resume only with a fresh capture after unlocking. Offline capture tests must cover partial success in monitor enumeration order, access denied on every monitor, no active monitors, unrelated capture failures and cancellation. Worker tests use an injected wait to verify repeated 60-second session checks without capture, model requests, bubble publication or speech-history writes while locked; preserved completed conversation, memory and failure counts; theme changes during rest; recovery only after an available session and a fresh successful capture; and ordinary one-second failures while already resting. Also verify cancellation of the default 60-second wait without waiting a minute, cancellation from capture observers, and detached callbacks when an application outlives its worker.
- Offline character tests must cover selected-theme loading, missing-file fallback to `loli_maid`, invalid existing character files, and edits to character contents between rounds and tool-feedback follow-ups while retaining fairy history. Desktop tests must also cover clearing the fairy conversation after a theme switch while retaining stored memories, keeping the pending round's character path, repeated selection of the same theme, and rapid switches back to the original theme. Integration fixtures must provide their own theme character files rather than `env/Character.md` and verify that the first fairy request after a switch contains no prior user, assistant or tool messages.
- For platform integration verification, run `REPO-ROOT/FatFish/UnitTest/Invoke.ps1` in PowerShell 7 after building `FatFishCli`. This opt-in test captures the desktop and uses only a local loopback fixture implemented by `Server.ps1` in the same folder.
- For desktop-window integration, run `FatFish/UnitTest/Invoke-Fairy.ps1` in PowerShell 7. It uses a temporary executable/theme/config/memory layout and a local loopback model fixture, and never copies real credentials. Verify transparency, animation, the startup balloon and its movement, dragging, theme menu order, theme switching and persistence, startup selection and fallback, continuous model rounds, updated and silent speech, error display and recovery, and menu exit during a pending model request. Pass `-ScreenshotPath PATH` to save menu screenshots for visual checks of the Chinese display names and selection mark. It moves the mouse during the test and restores the pointer afterwards.
- Keep the desktop visible and unlocked for regular integration and real-model verification. The separate opt-in `FatFish/UnitTest/Invoke-FairyRest.ps1` requires Debug x64 with its matching PDB and the Windows SDK x64 CDB. The user locks Windows before running it and keeps it locked until completion; the script never locks or unlocks the machine or injects capture failures. It uses synthetic configuration and a loopback listener without real credentials, and verifies two real WTS checks reporting a locked or non-active session about 60 seconds apart, no capture attempts or HTTP requests, bare `L` across both checks, the retained startup greeting, and clean shutdown within two seconds during the next rest. CDB records the session classification and the text passed to the actual UI label callback. Debugger traces, a JSON report and optional native `PrintWindow` rendering are retained under `.artifacts/capture-rest` (override with `-EvidenceDirectory`; select CDB with `-CdbPath`). These observations are not actual locked-screen screenshots. Run normal actual-screen verification after unlocking.
- Verification must include 10 consecutive successful `ENTER` rounds in `FatFishCli`, using the configured real models in one running process.
- Each round must finish the vision agent followed by the fairy agent successfully. After all 10 rounds, press `ESC` and verify a clean exit.
- If any round fails, fix the problem and restart the 10-round verification before reporting completion.
- `FatFishFairy` verification must also run the desktop app with the configured real models in one process and display at least two nonempty fairy speech results in its actual talking bubble within the first two minutes after startup. Inspect each visible bubble update on screen; parsed `speak` calls, logs and loopback fixtures do not satisfy this requirement. Confirm a clean exit after verification. If the two-minute requirement fails, fix the problem and restart this desktop verification before reporting completion.

## FatFishFairy

The application is based on GacUI, using `Release/Tools/GacBuild.ps1` to compile `FatFish/FatFishFairy/UI/Resource.xml` to `FatFish/FatFishFairy/UI/Source`, the `CppCompressed` options embed generated binary resources in `FatFishUIResource.cpp`. The application uses the ordinary Windows Direct2D renderer, without hosted mode. The project invokes GacBuild through the adjacent `UI/GacUI.xml` driver before compiling; bootstrap the ignored `GacGen.exe` and `CppMerge.exe` as described in `Release/Tools/README.md` if missing. Keep generated C++ files committed with the XML.

### Main Window

A frameless control template should be created for the main window.
The main window is 384x384 without border, this could be implemented with customized frame enabled.
The background should be #00FF00 so that when a png renders on it, the background color could be specified as a transparent color to make the transparent part in the png actually transparent.
`GetWindowsForm` could be used to get the `HWND` of the main window, with appropriate Windows API to implement the transparent feature.

### Behavior

The main window is always top-most.
Dragging the main window using left button moves the window and the talking bubble. Remember the local cursor position on left `mouseDown`; on `mouseMove`, move the current native bounds by the converted difference from that fixed anchor. GacUI handles capture automatically. Save the position on left `mouseUp`; do not initiate native caption dragging or manage capture manually.
Right click the main window shows a menu organized as below:
- `主题`: A menu placeholder defined in `UI/Resource.xml`; C++ creates and populates its submenu after loading the theme catalog:
  - List every theme in `themes/theme.json` order, using its Chinese display name instead of its folder key. Check the current theme's menu item.
  - Switching themes saves the exact folder key as `selectedTheme` in `config.json`, immediately chooses an animation from that theme, and displays its first frame with a fresh one-second playback interval.
  - At startup, restore the theme with the exact `selectedTheme` key. If the key is missing or does not match a catalog entry, use the first theme. A present value with a non-string type is a configuration error.
- `退出`: Exit the application.

Define the named `contextMenu` ToolstripMenu component, its theme placeholder and its exit action in `UI/Resource.xml`. C++ creates and populates the theme placeholder's submenu and opens the generated context menu in response to right-click.

`REPO-ROOT/env/config.json` looks like this
```JSON
{
  "windowX": 0,
  "windowY": 0,
  "selectedTheme": "loli_maid"
}
```
This file defines the initial location and selected theme of the main window. Save the location when dragging stops and save the theme when it changes, so both are restored at the next startup.
`config.json` is ignored by Git. Missing files or missing coordinates default to zero; dragging or switching themes creates the file as needed. Shared configuration persistence updates only the requested fields, preserving the other setting and unrelated fields. Coordinates are signed 32-bit desktop coordinates, including negative positions on other monitors. Malformed configuration, non-string `selectedTheme` values and missing or invalid theme frames fail explicitly.

### Talking Bubble

On startup, show `Hello, world!` in a native Win32 tracking balloon tooltip (`TOOLTIPS_CLASSW`, `TTS_BALLOON`, `TTF_TRACK`) just above the window, with its stem pointing down at the window's top center. Keep the latest nonempty speech or error visible until it is replaced or the window closes; an empty fairy result clears and hides the bubble until there is new text. Update its screen position from the GacUI `Moved()` callback, calling the base implementation first. Create and activate it when `WindowOpened` fires. Windows chooses stem direction automatically: use `TTM_TRACKPOSITION` at a point inside the related monitor's bottom edge to obtain its above-target layout, then measure the complete window with `GetWindowRect` and the stem's horizontal offset from that target. Cache the layout target to avoid moving the tooltip back to the monitor edge during ordinary dragging, and invalidate that layout when the text changes; use `SetWindowPos` to move the complete native shape above the fairy and clamp it to the monitor's work area. Use the first screen if the fairy is wholly off-screen. Do not use `TTF_ABSOLUTE`, which leaves the stem pointing upward when the whole balloon is placed above the fairy. Own and destroy its `HWND` in `FairyDesktopWindow`. The executable opts into common controls v6 for system visual styles and the current `TOOLINFO` layout. Do not use a polling timer for bubble positioning. The desktop integration test must inspect the balloon's native window region to verify its downward stem and target, as window bounds alone cannot detect a reversed pointer.

### Executing Agents

#### Progress indicator

- Keep a `SolidLabel` at the main window's lower-left corner, inset about six GUI units from the left and bottom. Inherit the window font, apply bold, and use skyblue `#87CEEB`. Do not expose progress through an extra window title.
- Display `V` while initializing, capturing, or running vision, and `F` while running the fairy, including tool feedback and context-recovery retries. Keep the last phase during failure display and the one-second retry delay; change to `V` when the next round starts. When the session or capture is unavailable, display bare `L` throughout rest and subsequent session checks or capture attempts until an available session and a fresh successful capture restore `V`.
- Append the count of failures since the last successful desktop round to `V` or `F`, omitting zero: `V`, `F`, `F1`, `F2`, `V2`, and so on. Count every observed context-limit error immediately, including recovered ones. Count other failures that end a round once, including initialization, session-query errors, ordinary capture errors, model and speech-history write failures. Session or capture unavailability neither increments nor resets the counter; `L` hides any existing count. The fourth context overflow already counts as a failure; aborting that round must not increment it again.
- Keep the counter across rounds, capture rests, history trimming, fresh fairy sessions and theme changes. Reset it immediately after a successful round, including silence, and after required speech-history persistence succeeds. The background runner owns phase and counter; publish updates to the UI thread without accessing the window from the worker or from callbacks after destruction.
- Offline tests verify exact phase/counter sequences and publication order. The desktop loopback fixture blocks requests to verify rendered skyblue glyphs at the lower-left, distinct `V`/`F`/`F1`/`F2`/`V1` states, counter retention across theme changes and reset after success. With `-ScreenshotPath`, save actual-screen `.progress-*.png` crops for visual verification of the letters and numbers; glyph-mask comparisons alone do not establish the displayed text.

#### Worker loop

`FatFishFairy` behaves like a user repeatedly pressing `ENTER` on `FatFishCli`:
- Start the vision-fairy-speak loop immediately after creating the startup bubble in `WindowOpened`.
- Run one round at a time on an owned background worker. Reuse the same `FairyApplication` and memory store; retain the fairy conversation until a theme switch resets it or context-overflow recovery trims or resets it. Each vision observation still starts a new session.
- After collecting the complete fairy speech, update the existing bubble on the UI thread, then immediately schedule the next round. An empty result clears and hides the bubble. Animation, dragging and menus must remain responsive during model requests.
- Display initialization, ordinary capture errors and model errors that end a round in the bubble with the prefix `调用大模型发生错误：`, instead of letting them escape from the worker. Keep recoverable tool errors in the existing model-feedback path. Retry after one second on failure so repeated errors do not create a tight loop; successful rounds have no added delay. Retry failed initialization so correcting configuration can recover without restarting the window. Capture unavailability uses the rest behavior below.
- On exit, stop scheduling rounds, cancel any pending network operation and join the worker before destroying its application or the window. Queued UI callbacks must not access a destroyed window.

#### Session and capture rest

- `IsDesktopSessionAvailable()` is the reusable native session query. It calls `WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, ...)` for the current process's session. It requires `WTSINFOEXW::Level == 1`, `SessionState == WTSActive` and `SessionFlags == WTS_SESSIONSTATE_UNLOCK`; `EnsureDesktopSessionAvailable()` converts an unavailable result into `ScreenCaptureUnavailable`. The overload accepting state and flags classifies injected values for offline tests. Validate the buffer size and level, free it with `WTSFreeMemory`, and treat query failures, unknown connection states or unknown lock flags in an active session as ordinary errors that prevent requests.
- Compare lock flags by equality: `WTS_SESSIONSTATE_LOCK` is zero, not a bit mask. A local lock or a lock in the app's connected RDP host session denies observation even if its connection remains `WTSActive`; an RDP disconnect or any other known non-active state also denies observation. Query the session running the app, not the RDP client computer's session or another user's console session. Never infer availability from a successful capture when the session check fails.
- Remote Desktop client state does not necessarily match the host session state. Locking only the client can leave the host session `WTSActive` + `WTS_SESSIONSTATE_UNLOCK` while capture is denied; disconnecting RDP can leave the process's session present as `WTSDisconnected` + `WTS_SESSIONSTATE_LOCK`, blocking capture at the session check. Reported combinations vary by environment. Monitor presence does not establish session availability. A true session result only permits capture; either agent requires fresh successful capture as well. Any round-level capture exception (including one after partially populating snapshots), empty capture, false session result or session-query error prevents both model roles from starting that round. A successful partial-monitor capture remains permitted as specified below.
- Check the session before and after capture, before and after each model request, before every tool, and before retaining the completed fairy round. Keep these checks outside tool-feedback/response-format error handlers so session unavailability enters rest. A request already dispatched when Windows locks may finish before the next check; after detecting the unavailable session, discard its response and the active partial round, with no further tools, requests, bubble result or speech-history entry for that round. Preserve completed historical rounds and already completed memory-tool effects. Resume from a fresh capture and observation after unlocking or reconnecting.
- In an available session, capture monitors independently and forward successful snapshots in their original enumeration order when at least one monitor succeeds. No active monitors or access denied on every monitor produces `ScreenCaptureUnavailable`. If every capture fails and any failure is unrelated to access denial, report that ordinary failure instead. Cancellation must still escape immediately.
- On `ScreenCaptureUnavailable`, show bare `L`, preserve the existing bubble, completed fairy conversation and memory, make no new model or tool HTTP requests and leave the failure counter unchanged. Wait 60 seconds on the worker with cancellation enabled, then schedule another session check without requiring a UI callback; capture only if the session is available. Do not publish a bubble result or append a speech-history entry during rest.
- Only an available session followed by a fresh successful capture clears `L`. `FairyApplication::CaptureSucceeded` fires after a nonempty capture and its following session check, before any vision request; cancellation by an observer must prevent that request. A session-query or ordinary capture error during rest keeps `L`, increments the internal failure count once, displays the ordinary error bubble and retries after one second. After capture succeeds, restore `V` and then `F` with the retained count; a complete successful round resets it as usual. Theme switching during rest still resets the fairy conversation before its next request while preserving memory and the failure count.
- The UI remains responsive during rest. Exiting cancels the wait immediately and joins the worker.

At each round's start, snapshot the selected theme's `Character.md` path and consume any pending theme-switch reset together under the runner's lock. A reset calls `FairyApplication::ResetFairySession` on the worker before running the new round, discarding all prior fairy conversation messages while preserving the memory store and saved memory files. Keep this round's character path for every fairy request and tool-feedback follow-up, with the `loli_maid` fallback described above. A theme switch during a pending round lets that round finish with its original theme, including displaying and logging its speech; the next round uses the latest selected theme and a fresh fairy session. Choosing the already selected theme does not reset the session. Switching away and back before the next round still requires a reset. The speech history log is preserved across theme changes.

Whenever the fairy speaks, append it to a git-ignored file `REPO-ROOT/env/history.md` in the following format:
```markdown
# Speak YYYY-MM-DD HH-mm-ss

content
```
Create this file if it doesn't exist.

Log one entry for each completed nonempty desktop fairy round, containing the complete speech accumulated across its tool calls and follow-ups. Use local system time when appending, with zero-padded fields and a 24-hour clock. Preserve the UTF-8 text and line breaks, separate entries with a blank line, and append without rewriting existing history. Startup greetings, vision observations, empty results and error messages are not speech history entries. `DesktopAgentRunner` performs this file I/O on its worker before publishing the result; write failures use the existing error bubble and retry path. `FatFishCli` does not write this desktop speech log.

Offline tests must cover history creation, UTF-8 multiline appends, preservation across reopening, timestamp formatting, silence, write failures and worker publication order. The desktop integration test must verify the saved complete speech, silent/error exclusions and preservation across restarts.

### Playing Animation

Theme assets are maintained separately from the desktop window implementation. Follow `themes/job.updateThemes.prompt.md` and mark an animation complete only after its files and metadata are verified. The catalog contains three themes with 30 animation series and 100 frames in total:

- `themes/loli_maid`: 10 series and 34 frames: `coffee`, `espresso`, `latte_art`, `homework`, `reading_manga`, `sleeping`, `playing`, `programming`, `drawing`, and `transformer`. `espresso` and `transformer` have five frames each; all other series have three.
- `themes/grown_maid`: 10 series and 36 frames: `coffee`, `espresso`, `latte_art`, `reading_phone`, `sleeping`, `programming`, `drawing`, `valkyrie`, `sailorfish`, and `teaching`. `espresso`, `valkyrie`, and `sailorfish` have five frames each; all other series have three.
- `themes/nurgling`: 10 series and 30 frames: `garden`, `cauldron`, `bell`, `flies`, `mushroom`, `snack`, `dance`, `sleeping`, `programming`, and `gift`. Every series has three frames.

In both maid themes, `espresso` has two puck-preparation frames followed by three latte-making frames. Respect explicit `xN` stage counts in the prompt. Frames use contiguous `<animation>_1.png` names, are 384×384 RGBA PNGs, and have a single connected white sticker backing with about 8 pixels of padding, a one-pixel `#E0E0E0` outer edge, and fully transparent exterior. Verify the actual alpha channel; a painted checkerboard is not transparency. Keep scale and placement consistent across an animation; use a stable outline for ordinary actions and let transformation outlines follow the changing body shape. Preserve existing character references and completed animations when adding series. `index.json` stores distinct frame counts, not playback repetition counts.

In `loli_maid/reading_manga`, the viewer sees the book's outer covers; a turning interior page rises behind those covers and stays attached to the binding. In `loli_maid/playing`, the character kneels with smooth knees in front and feet folded behind under the skirt. In both maid themes' `programming` series, the viewer sees the monitor's rear casing with blue `C++` lettering, never screen contents. Preserve these corrections when updating later frames.

The `grown_maid` main character has normal adult proportions, blue hair and eyes, a navy-and-white maid dress with a blue DeepSeek whale emblem, white thigh-high stockings, and a whale tail. Keep her identity consistent with the supplied reference through the five stages of each transformation. In `reading_phone`, she reclines on a sofa and the viewer sees the phone's back with a blue chibi whale maid decal; its screen faces her. In `sleeping`, she rests on a bed. In `drawing`, the pen display and monitor show her whale-maid self-portrait. In `teaching`, the adult holds a pointer at a whiteboard while the seated chibi whale maid student is seen only from behind.

The `nurgling` character is a cute green chibi creature with two horns, short limbs, a face mouth and a large cartoon belly mouth. Preserve the supplied `themes/nurgling/reference.png` unchanged and retain this identity in every animation. Each three-frame series uses a fixed connected white sticker outline and matching alpha channel, with consistent scale and placement. In `programming`, the viewer sees the monitor's rear casing without lettering or screen contents; the blue `C++` lettering rule applies only to the two maid themes.

The desktop player uses the selected theme, falling back to the first theme in `themes/theme.json` when no saved key matches. It selects an animation series randomly within that theme, shows one frame per second, and plays its complete frame sequence three consecutive times in total. It selects the next series randomly only after the last frame of the third playthrough, unless the user switches themes, and seeds its random generator afresh on each process start. Switching themes starts a fresh animation sequence immediately. `index.json` defines the distinct frame count. Playback is implemented by `Agents/Desktop.cpp` and the GUI timer, separately from the asset update job.

`FairyDesktopWindow` implements `INativeControllerListener::GlobalTimer` and updates the image only after at least 1000 monotonic milliseconds. Register the listener after initializing the window and unregister it in the derived destructor. GacUI already runs the global timer; do not start a separate timer or create an `IGuiAnimation` for frame playback.

## Important Learning

<!--
You can write anything in this section during development to make future works more efficient.
-->

- Current GacUI composition input uses `mouseDown`/`mouseUp` plus `GuiMouseEventArgs::button`, and `mouseMove` plus `arguments.left`. Event coordinates are GUI units; convert movement deltas with `INativeWindow::Convert` before updating native bounds. Keep the mouse-down anchor unchanged because moving the window updates the cursor's relative position.
- Use an adjacent `GacUI.xml` driver for GacBuild. Passing the resource itself as its driver makes its resource compiler replace the same `.log` folder that contains GacBuild's enumeration files.
- DarkSkin's menu template does not render a check for `Selected`; theme menu items show `✓` in their trailing `ShortcutText` slot and update it together with `Selected`.
- `WaitableObject::WaitAny` requires a non-null `abandoned` output pointer on Windows. For derived `vl::Thread` workers, verify shutdown using the native wait, since `Run()` does not automatically update `GetState()` to `Stopped`.
- Cancellable Chat Completions use asynchronous WinHTTP internally. Close a pending request only after its initiating API call returns, then wait for `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` before releasing callback state or read buffers.
- A locked Windows session can report `WTSActive` plus `WTS_SESSIONSTATE_LOCK` while screen capture succeeds and `OpenInputDesktop` still identifies the input desktop as `Default`. Capture failure counts and input-desktop names therefore do not identify locking reliably. Use the direct WTS session state; Windows 7's documented reversed lock flags are outside the supported OS range.
- `PrintWindow` can truncate painted native tooltip text despite correct stored text and complete on-screen rendering. Check long bubble text with the `.speech-desktop.png` capture from `Invoke-Fairy.ps1 -ScreenshotPath`, as well as text equality and native stem-region assertions.
- The default character avoids commenting on the fairy itself or repeating comments about an unchanged screen. Use changing, unrelated desktop content during real-model bubble verification so valid silence does not prevent the two visible speech updates.
- Antialiasing can make a progress glyph's exact skyblue pixel core shorter than the full rendered letter, especially `F`. Keep pixel-placement thresholds tolerant, compare distinct/stable glyph masks, and inspect the actual-screen progress crops to verify the text.
