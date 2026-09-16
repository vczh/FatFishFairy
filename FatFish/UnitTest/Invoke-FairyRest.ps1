#Requires -Version 7.0
param(
  [string]$CdbPath = '',
  [string]$EvidenceDirectory = ''
)

# Opt-in Debug x64 test of a genuinely locked Windows session. The user must lock
# the screen before running it and keep it locked until the test exits. This
# script never locks/unlocks Windows and never forces a capture failure.
# CDB observes the real WTS query, absence of capture, and progress-label callback.
# PrintWindow evidence is explicitly native rendering, not a locked-screen image.
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildOutput = Join-Path $repository 'FatFish/x64/Debug'
$executable = Join-Path $buildOutput 'FatFishFairy.exe'
if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath (Join-Path $buildOutput 'FatFishFairy.pdb'))) {
  throw 'Build Debug x64 FatFishFairy and its PDB before running the locked-desktop test.'
}
if ([string]::IsNullOrEmpty($CdbPath)) { $CdbPath = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Debuggers/x64/cdb.exe' }
if (-not (Test-Path -LiteralPath $CdbPath)) { throw 'Install the Windows SDK x64 Debugging Tools or pass -CdbPath.' }
if ([string]::IsNullOrEmpty($EvidenceDirectory)) { $EvidenceDirectory = Join-Path $repository '.artifacts/capture-rest' }
$EvidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
[void][IO.Directory]::CreateDirectory($EvidenceDirectory)
$fixture = Join-Path $EvidenceDirectory ('locked-' + [Guid]::NewGuid().ToString('N'))
$fixture = [IO.Path]::GetFullPath($fixture)
if (-not $fixture.StartsWith($EvidenceDirectory.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid locked-desktop fixture location.' }
$fixtureOutput = Join-Path $fixture 'FatFish/x64/Debug'
[void][IO.Directory]::CreateDirectory($fixtureOutput)
[void][IO.Directory]::CreateDirectory((Join-Path $fixture 'env'))
[void][IO.Directory]::CreateDirectory((Join-Path $fixture 'themes/loli_maid'))
Copy-Item -LiteralPath $executable -Destination (Join-Path $fixtureOutput 'FatFishFairy.exe')

# Reuse only the native helper class from the regular GUI fixture; do not run
# its PowerShell test flow or copy real artwork, prompts, configuration or keys.
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
if (-not ('FatFishFairySmoke.Native' -as [type])) {
  $helperScript = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Invoke-Fairy.ps1'))
  $helper = [regex]::Match($helperScript, "(?s)Add-Type -ReferencedAssemblies \`$drawingReferences -TypeDefinition @'\r?\n(?<source>.*?)\r?\n'@")
  if (-not $helper.Success) { throw 'Cannot find the native helper class in Invoke-Fairy.ps1.' }
  $drawingReferences = @([Drawing.Bitmap].Assembly.Location, [Drawing.Color].Assembly.Location)
  foreach ($reference in @('ref/System.Runtime.dll', 'ref/System.Collections.dll', 'ref/System.Security.Cryptography.dll', 'ref/System.Security.Cryptography.Algorithms.dll', 'System.Private.Windows.GdiPlus.dll', 'System.Private.Windows.Core.dll')) {
    $referencePath = Join-Path $PSHOME $reference
    if (Test-Path -LiteralPath $referencePath) { $drawingReferences += $referencePath }
  }
  Add-Type -ReferencedAssemblies $drawingReferences -TypeDefinition $helper.Groups['source'].Value
}
[void][FatFishFairySmoke.Native]::SetProcessDpiAwarenessContext([IntPtr](-4))
[IO.File]::WriteAllText((Join-Path $fixture 'themes/theme.json'), '{"loli_maid":"休眠测试"}', [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $fixture 'themes/loli_maid/index.json'), '{"sample":1}', [Text.UTF8Encoding]::new($false))
[FatFishFairySmoke.Native]::CreateThemeFrame((Join-Path $fixture 'themes/loli_maid/sample_1.png'), 0x2050E0, 1)
[IO.File]::WriteAllText((Join-Path $fixture 'themes/loli_maid/Character.md'), '# 中文休眠测试角色', [Text.UTF8Encoding]::new($false))
foreach ($name in @('Tools.md', 'Guidance.md', 'Request_Vision.md', 'Request_Fairy.md')) {
  [IO.File]::WriteAllText((Join-Path $fixture "env/$name"), "# 中文休眠测试 $name", [Text.UTF8Encoding]::new($false))
}
[IO.File]::WriteAllText((Join-Path $fixture 'env/config.json'), '{"windowX":123,"windowY":91}', [Text.UTF8Encoding]::new($false))

$probe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$probe.Start()
$port = $probe.LocalEndpoint.Port
$probe.Stop()
$configuration = @{ apikey = 'synthetic-rest-key'; url = "http://127.0.0.1:$port/v1"; auth_header = 'Authorization: Bearer $APIKEY'; vision_model = 'rest-vision'; fairy_model = 'rest-fairy' }
[IO.File]::WriteAllText((Join-Path $fixture 'env/apikey.json'), ($configuration | ConvertTo-Json), [Text.UTF8Encoding]::new($false))

# Resolve source-line tracepoints from the checked-out source matching the PDB.
# This avoids depending on generated lambda symbol names or changing the app.
$platformSource = Join-Path $repository 'Agents/Platform.cpp'
$mainSource = Join-Path $repository 'FatFish/FatFishFairy/Main.cpp'
$platformLines = [IO.File]::ReadAllLines($platformSource)
$mainLines = [IO.File]::ReadAllLines($mainSource)
$captureLine = 0
$unavailableLine = 0
$sessionCheckLine = 0
$sessionStateLine = 0
$progressLine = 0
for ($index = 0; $index -lt $platformLines.Length; $index++) {
  if ($platformLines[$index] -match '^\s*void CaptureMonitors\(List<MonitorSnapshot>& snapshots\)\s*$') { $captureLine = $index + 3 }
  if ($platformLines[$index] -match '^\s*ScreenCaptureUnavailable::ScreenCaptureUnavailable\(\)\s*$') { $unavailableLine = $index + 4 }
  if ($platformLines[$index].Contains('CheckPlatformResult(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,')) { $sessionCheckLine = $index + 1 }
  if ($platformLines[$index].Contains('return IsDesktopSessionAvailable(session.SessionState, session.SessionFlags);')) { $sessionStateLine = $index + 1 }
}
for ($index = 0; $index -lt $mainLines.Length; $index++) {
  if ($mainLines[$index].Contains('window->progressLabel->SetText(text);')) { $progressLine = $index + 1; break }
}
if (@($captureLine, $unavailableLine, $sessionCheckLine, $sessionStateLine, $progressLine) -contains 0) { throw 'Cannot locate session/capture/progress tracepoints in the current sources.' }
$commands = @(
  '.lines -e',
  ('bp `FatFishFairy!' + $platformSource + ':' + $sessionCheckLine + '` ".echo REST_SESSION_CHECK; .time; gc"'),
  ('bp `FatFishFairy!' + $platformSource + ':' + $sessionStateLine + '` ".printf \"REST_SESSION_STATE session=%u state=%d flags=%d\\n\", @@c++(session->SessionId), @@c++((int)session->SessionState), @@c++((int)session->SessionFlags); gc"'),
  ('bp `FatFishFairy!' + $platformSource + ':' + $captureLine + '` ".echo REST_CAPTURE; gc"'),
  ('bp `FatFishFairy!' + $platformSource + ':' + $unavailableLine + '` ".echo REST_UNAVAILABLE; gc"'),
  ('bp `FatFishFairy!' + $mainSource + ':' + $progressLine + '` ".printf \"REST_UI %mu\\n\", @@c++(this->text.buffer + this->text.start); gc"'),
  '.echo REST_TRACE_READY',
  'g'
)
$commandPath = Join-Path $fixture 'trace.cdb'
$tracePath = Join-Path $fixture 'trace.log'
[IO.File]::WriteAllLines($commandPath, $commands, [Text.UTF8Encoding]::new($false))
$listener = [Net.HttpListener]::new()
$listener.Prefixes.Add("http://127.0.0.1:$port/")
$debugger = $null
$application = $null
$window = $null
$report = [ordered]@{
  passed = $false; evidenceDirectory = $fixture; applicationId = 0; modelRequests = 0
  sessionChecks = 0; captureAttempts = 0; unavailableOutcomes = 0; retryIntervalMilliseconds = 0
  sessions = @(); sessionEvidence = 'CDB trace of the real WTS session query and availability decision; no state injection'
  uiProgress = @(); uiTextEvidence = 'CDB trace at the actual Main.cpp progress-label SetText call'
  nativeRenderingPath = ''; nativeRenderingError = ''; actualLockedScreenImage = $false
  greetingPreserved = $false; closeMilliseconds = 0; applicationExitCode = $null; error = ''
}

function Read-Trace {
  if (-not (Test-Path -LiteralPath $tracePath)) { return '' }
  $stream = [IO.File]::Open($tracePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
  $reader = [IO.StreamReader]::new($stream)
  try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}

function Assert-NoModelRequest {
  if ($script:pendingRequest.IsCompleted) {
    $report.modelRequests++
    $request = $script:pendingRequest.GetAwaiter().GetResult()
    # Do not read or save screenshot request bodies when the precondition fails.
    $request.Response.StatusCode = 503
    $request.Response.Close()
    throw 'The fairy issued a model request while this test expected session rest. Inspect the WTS session evidence in report.json and trace.log.'
  }
}

function Read-SessionEvidence([string]$trace) {
  $report.sessionChecks = [regex]::Matches($trace, '(?m)^REST_SESSION_CHECK\r?$').Count
  $report.captureAttempts = [regex]::Matches($trace, '(?m)^REST_CAPTURE\r?$').Count
  $report.unavailableOutcomes = [regex]::Matches($trace, '(?m)^REST_UNAVAILABLE\r?$').Count
  $report.uiProgress = @([regex]::Matches($trace, '(?m)^REST_UI ([A-Z][0-9]*)\r?$') | ForEach-Object { $_.Groups[1].Value })
  $report.sessions = @([regex]::Matches($trace, '(?ms)^REST_SESSION_CHECK\r?$(?:(?!^REST_SESSION_CHECK\r?$).)*') | ForEach-Object {
    $block = $_.Value
    $state = [regex]::Match($block, '(?m)^REST_SESSION_STATE session=(\d+) state=(\d+) flags=(-?\d+)\r?$')
    [ordered]@{
      queryCompleted = $state.Success
      sessionId = $(if ($state.Success) { [uint32]$state.Groups[1].Value } else { $null })
      state = $(if ($state.Success) { [int]$state.Groups[2].Value } else { $null })
      flags = $(if ($state.Success) { [int]$state.Groups[3].Value } else { $null })
      lockedOrInactive = $state.Success -and ($state.Groups[2].Value -ne '0' -or $state.Groups[3].Value -eq '0')
      unavailable = $block -match '(?m)^REST_UNAVAILABLE\r?$'
    }
  })
}

function Assert-SessionEvidence([string]$trace) {
  Read-SessionEvidence $trace
  if ($report.sessionChecks -ne 2 -or $report.unavailableOutcomes -ne 2) { throw 'Expected exactly two real unavailable Windows session checks.' }
  if ($report.captureAttempts -ne 0) { throw 'The locked or inactive session reached monitor capture instead of resting before capture.' }
  foreach ($session in $report.sessions) {
    if (-not $session.queryCompleted -or -not $session.lockedOrInactive -or -not $session.unavailable) { throw 'Each session check must report real WTS locked or inactive state followed by session unavailability.' }
  }
  $times = @([regex]::Matches($trace, '(?m)^REST_SESSION_CHECK\r?\nDebug session time: [^\r\n]+\r?\nSystem Uptime: [^\r\n]+\r?\nProcess Uptime: (\d+) days (\d+):(\d+):(\d+)\.(\d+)') | ForEach-Object {
    ([double]$_.Groups[1].Value * 86400000) + ([double]$_.Groups[2].Value * 3600000) + ([double]$_.Groups[3].Value * 60000) + ([double]$_.Groups[4].Value * 1000) + [double]$_.Groups[5].Value
  })
  if ($times.Count -ne 2) { throw 'CDB did not record both session check timestamps.' }
  $report.retryIntervalMilliseconds = [Math]::Round($times[1] - $times[0])
  if ($report.retryIntervalMilliseconds -lt 59000 -or $report.retryIntervalMilliseconds -gt 65000) { throw 'The next real Windows session check did not occur after the 60-second rest.' }
  if (($report.uiProgress -join ',') -cne 'V,L') { throw 'Expected V then bare L, with L preserved across the next unavailable session check.' }
}

function Read-Greeting {
  $windows = @([FatFishFairySmoke.Native]::Windows($application.Id))
  $balloon = $windows | Where-Object ClassName -eq 'tooltips_class32' | Select-Object -First 1
  if ($null -eq $balloon) { throw 'The stored startup greeting disappeared during session rest.' }
  return [FatFishFairySmoke.Native]::ControlText($balloon.Handle)
}

try {
  $listener.Start()
  $script:pendingRequest = $listener.GetContextAsync()
  $debugger = Start-Process -FilePath $CdbPath -WindowStyle Hidden -PassThru -ArgumentList @('-G', '-y', ('"' + $buildOutput + '"'), '-logo', ('"' + $tracePath + '"'), '-cf', ('"' + $commandPath + '"'), ('"' + (Join-Path $fixtureOutput 'FatFishFairy.exe') + '"'))
  $clock = [Diagnostics.Stopwatch]::StartNew()
  $firstUnavailable = $false
  $nativeCaptured = $false
  while ($clock.Elapsed.TotalSeconds -lt 100) {
    Assert-NoModelRequest
    if ($null -eq $application) {
      $child = Get-CimInstance Win32_Process -Filter "ParentProcessId = $($debugger.Id)" | Where-Object Name -eq 'FatFishFairy.exe' | Select-Object -First 1
      if ($null -ne $child) {
        $application = Get-Process -Id $child.ProcessId
        [void]$application.Handle # Retain the native handle so ExitCode remains available after exit.
        $report.applicationId = $application.Id
      }
    }
    if ($null -ne $application) {
      if ($application.HasExited) { throw "The fairy exited before rest verification, code $($application.ExitCode)." }
      if ($null -eq $window) { $window = [FatFishFairySmoke.Native]::Windows($application.Id) | Where-Object { $_.ClassName -eq 'VczhWindow' -and $_.Width -ge 300 -and $_.Height -ge 300 } | Select-Object -First 1 }
    }
    if ($debugger.HasExited) { throw 'CDB exited before the session-rest test completed.' }
    $trace = Read-Trace
    if ($trace -match '(?m)^(?:Ambiguous symbol error|Syntax error|Unable to resolve|Couldn.t resolve|No type information)|Extra character error|Type is not struct/class/union') { throw 'CDB could not resolve an observation tracepoint; inspect trace.log.' }
    Read-SessionEvidence $trace
    if ($report.captureAttempts -ne 0) { throw 'The session gate allowed monitor capture during the locked-session test.' }
    if ($report.unavailableOutcomes -ge 1 -and $report.uiProgress.Count -gt 0 -and $report.uiProgress[-1] -ceq 'L' -and $null -ne $window) {
      if ((Read-Greeting) -cne 'Hello, world!') { throw 'Session rest replaced the existing bubble text.' }
      $firstUnavailable = $true
      if (-not $nativeCaptured) {
        $nativeCaptured = $true
        try {
          $nativePath = Join-Path $fixture 'rest-L.native.png'
          [void][FatFishFairySmoke.Native]::Capture($window.Handle, $nativePath)
          $report.nativeRenderingPath = $nativePath
        } catch { $report.nativeRenderingError = 'PrintWindow unavailable: ' + $_.Exception.GetType().Name }
      }
    }
    if ($report.sessionChecks -ge 2 -and $report.unavailableOutcomes -ge 2 -and $firstUnavailable) { break }
    if (-not $firstUnavailable -and $clock.Elapsed.TotalSeconds -gt 30) { throw 'No real locked or inactive session outcome with UI label L was observed within 30 seconds.' }
    Start-Sleep -Milliseconds 100
  }
  Assert-NoModelRequest
  Assert-SessionEvidence (Read-Trace)
  $report.greetingPreserved = (Read-Greeting) -ceq 'Hello, world!'
  if (-not $report.greetingPreserved) { throw 'The greeting was not preserved across both rests.' }
  Start-Sleep -Milliseconds 250
  Assert-NoModelRequest
  $closing = [Diagnostics.Stopwatch]::StartNew()
  [void][FatFishFairySmoke.Native]::PostMessage($window.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
  if (-not $application.WaitForExit(2000)) { throw 'Closing the fairy did not cancel its second 60-second rest within two seconds.' }
  $report.closeMilliseconds = $closing.ElapsedMilliseconds
  $report.applicationExitCode = $application.ExitCode
  if ($application.ExitCode -ne 0) { throw 'The fairy did not exit cleanly after cancelling session rest.' }
  if (-not $debugger.WaitForExit(5000)) { throw 'CDB did not exit after the fairy closed.' }
  Assert-NoModelRequest
  $finalTrace = Read-Trace
  Assert-SessionEvidence $finalTrace
  if ($finalTrace.Contains('Detected memory leaks!')) { throw 'Debug session-rest verification reported a memory leak.' }
  $report.passed = $true
} catch {
  $report.error = $_.Exception.Message
  throw
} finally {
  if ($null -ne $application -and -not $application.HasExited -and $null -ne $window) {
    [void][FatFishFairySmoke.Native]::PostMessage($window.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    [void]$application.WaitForExit(2000)
  }
  if ($null -ne $debugger -and -not $debugger.HasExited) { $debugger.Kill(); [void]$debugger.WaitForExit(5000) }
  # Failures can precede the next polling read. Retain the latest native session
  # and capture evidence on every exit path.
  Read-SessionEvidence (Read-Trace)
  $listener.Stop()
  $listener.Close()
  [IO.File]::WriteAllText((Join-Path $fixture 'report.json'), ($report | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
  $report | ConvertTo-Json -Depth 8
}
