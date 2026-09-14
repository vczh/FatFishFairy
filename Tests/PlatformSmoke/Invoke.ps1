#Requires -Version 7.0
param(
  [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
  [ValidateSet('x64', 'Win32')][string]$Platform = 'x64'
)

# Opt-in integration test: captures the desktop, sends PNGs only to a local
# loopback fixture, and decodes them in memory. No screenshots are saved.
# Build the selected FatFishCli configuration before invoking this script.
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$projectUser = Join-Path $repository 'FatFish/FatFishCli/FatFishCli.vcxproj.user'
$hadSettings = Test-Path -LiteralPath $projectUser
$originalSettings = [byte[]]::new(0)
if ($hadSettings) { $originalSettings = [IO.File]::ReadAllBytes($projectUser) }
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('FatFishPlatformSmoke-' + [Guid]::NewGuid().ToString('N'))
$server = $null
$locationPushed = $false
try {
  [void][IO.Directory]::CreateDirectory((Join-Path $fixture 'env'))
  $probe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
  $probe.Start()
  $port = $probe.LocalEndpoint.Port
  $probe.Stop()
  $configurationData = @{ apikey = 'synthetic-platform-key'; url = "http://127.0.0.1:$port/v1"; auth_header = 'Authorization: Bearer $APIKEY'; vision_model = 'smoke-vision'; fairy_model = 'smoke-fairy' }
  [IO.File]::WriteAllText((Join-Path $fixture 'env/apikey.json'), ($configurationData | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
  foreach ($name in @('Tools.md', 'Guidance.md', 'Request_Vision.md', 'Request_Fairy.md', 'Character.md')) {
    [IO.File]::WriteAllText((Join-Path $fixture "env/$name"), "# 中文测试 $name", [Text.UTF8Encoding]::new($false))
  }

  [xml]$settings = if ($hadSettings) { [IO.File]::ReadAllText($projectUser) } else { '<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003" />' }
  $group = $settings.DocumentElement.SelectNodes('*[local-name()="PropertyGroup"]') | Where-Object { $_.GetAttribute('Condition').Contains("$Configuration|$Platform") } | Select-Object -First 1
  if ($null -eq $group) {
    $group = $settings.CreateElement('PropertyGroup', $settings.DocumentElement.NamespaceURI)
    $group.SetAttribute('Condition', ('''$(Configuration)|$(Platform)''==''' + $Configuration + '|' + $Platform + ''''))
    [void]$settings.DocumentElement.AppendChild($group)
  }
  $arguments = $group.SelectSingleNode('*[local-name()="LocalDebuggerCommandArguments"]')
  if ($null -eq $arguments) {
    $arguments = $settings.CreateElement('LocalDebuggerCommandArguments', $settings.DocumentElement.NamespaceURI)
    [void]$group.AppendChild($arguments)
  }
  $arguments.InnerText = '--once --repo-root "' + $fixture + '"'
  $settings.Save($projectUser)

  $shell = (Get-Process -Id $PID).Path
  $server = Start-Process -FilePath $shell -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile', '-File', ('"' + (Join-Path $PSScriptRoot 'Server.ps1') + '"'), '-FixtureRoot', ('"' + $fixture + '"'), '-Port', $port)
  $ready = Join-Path $fixture 'ready'
  for ($attempt = 0; $attempt -lt 100 -and -not (Test-Path -LiteralPath $ready); $attempt++) {
    if ($server.HasExited) { throw 'The loopback fixture exited before becoming ready.' }
    Start-Sleep -Milliseconds 100
  }
  if (-not (Test-Path -LiteralPath $ready)) { throw 'The loopback fixture failed to become ready.' }
  Push-Location (Join-Path $repository 'FatFish')
  $locationPushed = $true
  $applicationOutput = @(& (Join-Path $repository 'Release/.github/Scripts/copilotExecute.ps1') -Mode CLI -Executable FatFishCli -Configuration $Configuration -Platform $Platform)
  $applicationExitCode = $LASTEXITCODE
  $applicationOutput | ForEach-Object { Write-Output $_ }
  if (-not $server.WaitForExit(5000)) { throw 'The loopback fixture did not finish.' }
  $report = [IO.File]::ReadAllText((Join-Path $fixture 'report.json')) | ConvertFrom-Json
  $report | ConvertTo-Json -Depth 8
  if ($applicationExitCode -ne 0 -or -not $report.passed) { throw 'Platform integration failed; see the sanitized report above.' }
  $responseLines = @($applicationOutput | Where-Object { $_ -match '^(Vision|Fairy)> ' })
  if ($responseLines.Count -ne 4) { throw 'Expected exactly four JSON response log lines.' }
  for ($index = 0; $index -lt $responseLines.Count; $index++) {
    $expectedPrefix = if ($index -lt 2) { 'Vision> ' } else { 'Fairy> ' }
    if (-not $responseLines[$index].StartsWith($expectedPrefix)) { throw 'Incorrect response agent label.' }
    $message = $responseLines[$index].Substring($expectedPrefix.Length) | ConvertFrom-Json -Depth 30
    if ($message.role -ne 'assistant' -or $null -ne $message.choices) { throw 'Expected a complete assistant JSON message.' }
    if ($index -eq 0 -and $message.tool_calls[0].function.name -ne 'speak') { throw 'Vision speak was omitted from the JSON log.' }
    if ($index -eq 2 -and $message.tool_calls.Count -ne 3) { throw 'Fairy tool calls were omitted from the JSON log.' }
    if ($index -in @(1, 3) -and $message.content -ne '') { throw 'Expected an empty final message in the JSON log.' }
  }
  if ($applicationOutput -contains '本地平台集成测试通过。' -or $applicationOutput -contains '本地屏幕捕获和 PNG 编码验证通过。') {
    throw 'Speak must not produce a separate plain-text line.'
  }
  Write-Output 'All CLI JSON output checks passed.'
} finally {
  if ($locationPushed) { Pop-Location }
  if ($null -ne $server -and -not $server.HasExited) { $server.Kill(); [void]$server.WaitForExit(5000) }
  if ($hadSettings) {
    [IO.File]::WriteAllBytes($projectUser, $originalSettings)
  } elseif (Test-Path -LiteralPath $projectUser) {
    Remove-Item -LiteralPath $projectUser -Force
  }
  $fixtureFull = [IO.Path]::GetFullPath($fixture)
  $temporaryPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
  if (-not $fixtureFull.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or -not [IO.Path]::GetFileName($fixtureFull).StartsWith('FatFishPlatformSmoke-')) {
    throw 'Unsafe platform fixture cleanup path.'
  }
  if (Test-Path -LiteralPath $fixtureFull) { Remove-Item -LiteralPath $fixtureFull -Recurse -Force }
}
