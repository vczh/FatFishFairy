#Requires -Version 7.0
param(
  [Parameter(Mandatory)][string]$FixtureRoot,
  [Parameter(Mandatory)][int]$Port
)

# Each response waits for release-N, allowing the native GUI test to inspect the
# completed round while the next network request remains in flight. Captured
# screen data is read only in memory and is never included in reports.
$ErrorActionPreference = 'Stop'
$listener = [Net.HttpListener]::new()
$listener.Prefixes.Add("http://127.0.0.1:$Port/")
$data = [IO.File]::ReadAllText((Join-Path $FixtureRoot 'data.json')) | ConvertFrom-Json
$observations = [Collections.Generic.List[string]]::new()
$context = $null

function Assert-Fixture([bool]$condition, [string]$message) {
  if (-not $condition) { throw ('Fairy fixture assertion: ' + $message) }
}

function New-Speech([string]$id, [string]$text) {
  return @{ id = $id; type = 'function'; function = @{ name = 'speak'; arguments = (@{ text = $text } | ConvertTo-Json -Compress) } }
}

function Wait-Release([int]$sequence) {
  $deadline = [DateTime]::UtcNow.AddMinutes(2)
  while (-not (Test-Path -LiteralPath (Join-Path $FixtureRoot "release-$sequence"))) {
    if ([DateTime]::UtcNow -ge $deadline) { throw 'Timed out waiting for the GUI test to release a response.' }
    Start-Sleep -Milliseconds 25
  }
}

function Send-Completion($context, [object[]]$calls) {
  $context.Response.StatusCode = 200
  $context.Response.ContentType = 'text/event-stream; charset=utf-8'
  $context.Response.SendChunked = $true
  $chunks = @(@{ choices = @(@{ index = 0; delta = @{ role = 'assistant'; content = '普通回复不应出现在气泡中。' }; finish_reason = $null }) })
  for ($index = 0; $index -lt $calls.Count; $index++) {
    $call = $calls[$index]
    $arguments = $call.function.arguments
    $split = [int][Math]::Floor($arguments.Length / 2)
    $chunks += @{ choices = @(@{ index = 0; delta = @{ tool_calls = @(@{ index = $index; id = $call.id; type = 'function'; function = @{ name = $call.function.name; arguments = $arguments.Substring(0, $split) } }) }; finish_reason = $null }) }
    $chunks += @{ choices = @(@{ index = 0; delta = @{ tool_calls = @(@{ index = $index; function = @{ arguments = $arguments.Substring($split) } }) }; finish_reason = $null }) }
  }
  $finish = if ($calls.Count -gt 0) { 'tool_calls' } else { 'stop' }
  $chunks += @{ choices = @(@{ index = 0; delta = @{}; finish_reason = $finish }) }
  foreach ($chunk in $chunks) {
    $bytes = [Text.Encoding]::UTF8.GetBytes('data: ' + ($chunk | ConvertTo-Json -Depth 30 -Compress) + "`r`n`r`n")
    $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
    $context.Response.OutputStream.Flush()
  }
  $bytes = [Text.Encoding]::UTF8.GetBytes("data: [DONE]`r`n`r`n")
  $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
  $context.Response.Close()
}

try {
  $listener.Start()
  [IO.File]::WriteAllText((Join-Path $FixtureRoot 'ready'), 'ready')
  for ($sequence = 1; $sequence -le 16; $sequence++) {
    $pending = $listener.GetContextAsync()
    if (-not $pending.Wait(120000)) { throw 'Timed out waiting for a desktop completion request.' }
    $context = $pending.Result
    Assert-Fixture ([Net.IPAddress]::IsLoopback($context.Request.RemoteEndPoint.Address)) 'Only loopback requests are accepted.'
    if ($sequence -eq 16) {
      Assert-Fixture ($context.Request.HttpMethod -eq 'GET' -and $context.Request.RawUrl -eq '/knowledge') 'Expected the anonymous tool request.'
      Assert-Fixture ([string]::IsNullOrEmpty($context.Request.Headers['Authorization'])) 'The knowledge request must not include model credentials.'
      [IO.File]::WriteAllText((Join-Path $FixtureRoot "pending-$sequence"), 'http_get')
      Wait-Release $sequence
      $context.Response.StatusCode = 200
      $context.Response.Close()
      $context = $null
      continue
    }
    Assert-Fixture ($context.Request.HttpMethod -eq 'POST' -and $context.Request.RawUrl -eq '/v1/chat/completions') 'Expected the Chat Completions endpoint.'
    Assert-Fixture ($context.Request.Headers['Authorization'] -ceq 'Bearer synthetic-fairy-key') 'Expected synthetic authorization.'
    $reader = [IO.StreamReader]::new($context.Request.InputStream, [Text.Encoding]::UTF8)
    try { $payload = $reader.ReadToEnd() | ConvertFrom-Json -Depth 100 } finally { $reader.Dispose() }
    Assert-Fixture ($payload.stream -eq $true) 'Expected streaming.'
    Assert-Fixture ($payload.tools.Count -eq 7) 'Expected all tools.'
    Assert-Fixture ($payload.messages[0].content.Contains('中文桌面测试')) 'Expected isolated Chinese prompts.'
    $vision = $sequence -in @(1, 2, 6, 7, 10, 11, 12, 15)
    $expectedModel = if ($vision) { 'desktop-vision' } else { 'desktop-fairy' }
    Assert-Fixture ($payload.model -ceq $expectedModel) "Incorrect agent ordering at request $sequence."
    if ($sequence -in @(1, 6, 10, 11, 15)) {
      Assert-Fixture ($payload.messages.Count -eq 2) 'Every vision round must start with a fresh session.'
      $parts = $payload.messages[1].content
      Assert-Fixture ($parts.Count -ge 2 -and $parts.Count % 2 -eq 0) 'Expected monitor metadata and snapshots.'
      for ($index = 0; $index -lt $parts.Count; $index += 2) {
        Assert-Fixture ($parts[$index].type -eq 'text' -and $parts[$index + 1].type -eq 'image_url') 'Expected a labeled image for each monitor.'
        Assert-Fixture ($parts[$index + 1].image_url.url.StartsWith('data:image/png;base64,iVBORw0KGgo')) 'Expected a PNG snapshot.'
      }
      $parts = $null
    }
    if (-not $vision) {
      $character = switch ($sequence) {
        { $_ -in @(3, 4, 5) } { $data.firstCharacter }
        { $_ -in @(8, 9) } { $data.fallbackCharacter }
        { $_ -in @(13, 14) } { $data.secondCharacter }
      }
      $expectedSystem = "# 中文桌面测试 Tools.md`n`n# 中文桌面测试 Guidance.md`n`n# 中文桌面测试 Request_Fairy.md`n`n" + $character
      Assert-Fixture ($payload.messages[0].content -ceq $expectedSystem) "Incorrect selected-theme character or fallback at request $sequence."
      $userMessages = @($payload.messages | Where-Object role -eq 'user')
      $newObservation = $sequence -in @(3, 8, 13)
      if ($newObservation) {
        # Each tested round follows startup or a theme switch, including one
        # followed by a failed vision request. No old assistant/tool messages
        # may remain even if their corresponding observations were removed.
        Assert-Fixture ($payload.messages.Count -eq 2) "Expected a fresh fairy session after startup or theme switching at request $sequence."
        $observations.Clear()
      }
      Assert-Fixture ($userMessages.Count -eq ($observations.Count + [int]$newObservation)) 'Fairy history was reset within a round or duplicated.'
      for ($index = 0; $index -lt $observations.Count; $index++) {
        Assert-Fixture ($userMessages[$index].content -ceq $observations[$index]) 'A prior fairy timestamp or observation changed.'
      }
      if ($newObservation) {
        $expectedObservation = switch ($sequence) { 3 { $data.visionFirst }; 8 { '第二轮观察。' }; 13 { '错误之后的新观察。' } }
        $observation = $userMessages[-1].content
        Assert-Fixture ($observation -cmatch '^当前日期时间是：[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}-[0-9]{2}-[0-9]{2}\n以下是用户所有屏幕的内容：\n') 'Expected the local timestamp and observation label.'
        Assert-Fixture ($observation.EndsWith("`n" + $expectedObservation, [StringComparison]::Ordinal)) 'The complete vision speech did not reach the fairy.'
        $observations.Add($observation)
      }
    }
    foreach ($reply in @($payload.messages | Where-Object role -eq 'tool')) {
      Assert-Fixture (($reply.content | ConvertFrom-Json).ok -eq $true) 'A speech tool returned an error.'
    }
    $payload = $null
    [IO.File]::WriteAllText((Join-Path $FixtureRoot "pending-$sequence"), $expectedModel)
    Wait-Release $sequence
    if ($sequence -eq 10) {
      $context.Response.StatusCode = 503
      $bytes = [Text.Encoding]::UTF8.GetBytes('{"error":{"message":"synthetic desktop failure"}}')
      $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
      $context.Response.Close()
    } else {
      $calls = @(switch ($sequence) {
        1 { New-Speech 'vision-first' '第一段完整观察。'; New-Speech 'vision-second' '第二段完整观察。' }
        3 { New-Speech 'fairy-long' $data.longSpeech; New-Speech 'fairy-empty' ''; New-Speech 'fairy-next' $data.secondSpeech }
        4 { New-Speech 'fairy-follow-up' $data.followUpSpeech }
        6 { New-Speech 'vision-round-two' '第二轮观察。' }
        8 { New-Speech 'fairy-silent' '' }
        11 { New-Speech 'vision-recovered' '错误之后的新观察。' }
        13 { New-Speech 'fairy-recovered' $data.recoveredSpeech }
        15 {
          New-Speech 'vision-before-http' '等待本地知识查询。'
          @{ id = 'pending-http'; type = 'function'; function = @{ name = 'http_get'; arguments = (@{ url = "http://127.0.0.1:$Port/knowledge" } | ConvertTo-Json -Compress) } }
        }
      })
      Send-Completion $context $calls
    }
    $context = $null
  }
} catch {
  # Never include request bodies or screenshots in diagnostics.
  $message = if ($_.Exception.Message.StartsWith('Fairy fixture assertion: ')) { $_.Exception.Message } else { 'Fixture error at line ' + $_.InvocationInfo.ScriptLineNumber + ': ' + $_.Exception.GetType().Name }
  [IO.File]::WriteAllText((Join-Path $FixtureRoot 'error.txt'), $message)
} finally {
  if ($null -ne $context) { try { $context.Response.Close() } catch { } }
  $listener.Stop()
  $listener.Close()
}
