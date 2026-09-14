#Requires -Version 7.0
param([Parameter(Mandatory)][string]$FixtureRoot, [Parameter(Mandatory)][int]$Port)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SmokeDpi {
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
[void][SmokeDpi]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
$listener = [System.Net.HttpListener]::new()
$listener.Prefixes.Add("http://127.0.0.1:$Port/")
$report = [ordered]@{ passed = $false; posts = 0; anonymousGets = 0; monitors = @(); toolReplies = 0; error = '' }
function Assert-Smoke([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw ('Smoke assertion: ' + $Message) }
}
function Send-Json($Context, $Object) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($Object | ConvertTo-Json -Depth 30 -Compress))
  $Context.Response.StatusCode = 200
  $Context.Response.ContentType = 'application/json; charset=utf-8'
  $Context.Response.ContentLength64 = $bytes.Length
  $Context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
  $Context.Response.Close()
}
function Make-Tool([string]$Id, [string]$Name, $Arguments) {
  return @{ id = $Id; type = 'function'; function = @{ name = $Name; arguments = ($Arguments | ConvertTo-Json -Compress) } }
}
try {
  $listener.Start()
  [IO.File]::WriteAllText((Join-Path $FixtureRoot 'ready'), 'ready')
  $screenCount = [System.Windows.Forms.Screen]::AllScreens.Count
  while ($report.posts -lt 3) {
    $pending = $listener.GetContextAsync()
    if (-not $pending.Wait(120000)) { throw 'Timed out waiting for the local fixture request.' }
    $context = $pending.Result
    Assert-Smoke ([Net.IPAddress]::IsLoopback($context.Request.RemoteEndPoint.Address)) 'Only loopback requests are accepted.'
    if ($context.Request.HttpMethod -eq 'GET') {
      Assert-Smoke ($context.Request.RawUrl -eq '/knowledge') 'Unexpected GET path.'
      Assert-Smoke ([string]::IsNullOrEmpty($context.Request.Headers['Authorization'])) 'Anonymous GET unexpectedly carried API authorization.'
      Assert-Smoke ([string]::IsNullOrEmpty($context.Request.Headers['Cookie'])) 'Anonymous GET unexpectedly carried a cookie.'
      $report.anonymousGets++
      Send-Json $context @{ knowledge = '本地知识测试通过' }
      continue
    }
    Assert-Smoke ($context.Request.HttpMethod -eq 'POST') 'Unexpected HTTP method.'
    Assert-Smoke ($context.Request.RawUrl -eq '/v1/chat/completions') 'Incorrect Chat Completions endpoint path.'
    Assert-Smoke ($context.Request.Headers['Authorization'] -eq 'Bearer synthetic-platform-key') 'Authentication template was not substituted correctly.'
    Assert-Smoke ($context.Request.ContentType -match '^application/json') 'Missing JSON request content type.'
    $reader = [IO.StreamReader]::new($context.Request.InputStream, [Text.Encoding]::UTF8)
    $payload = $reader.ReadToEnd() | ConvertFrom-Json -Depth 100
    $reader.Dispose()
    Assert-Smoke ($payload.stream -eq $false) 'Expected a non-streaming completion request.'
    Assert-Smoke ($payload.tools.Count -eq 7) 'Both models should receive the complete tool schema.'
    Assert-Smoke ($payload.messages[0].content.Contains('中文测试')) 'UTF-8 Chinese prompt text was corrupted.'
    $report.posts++
    if ($report.posts -eq 1) {
      Assert-Smoke ($payload.model -eq 'smoke-vision') 'First request must use the vision model.'
      Assert-Smoke ($payload.messages.Count -eq 2) 'Vision must start with a fresh session.'
      $parts = $payload.messages[1].content
      Assert-Smoke ($parts.Count -eq (2 * $screenCount)) 'Missing a connected monitor image or its metadata.'
      for ($index = 0; $index -lt $parts.Count; $index += 2) {
        Assert-Smoke ($parts[$index].type -eq 'text') 'Missing monitor metadata.'
        Assert-Smoke ($parts[$index + 1].type -eq 'image_url') 'Missing image URL part.'
        $dataUrl = $parts[$index + 1].image_url.url
        Assert-Smoke ($dataUrl.StartsWith('data:image/png;base64,')) 'Screenshot must be a PNG data URL.'
        $bytes = [Convert]::FromBase64String($dataUrl.Substring(22))
        Assert-Smoke ($bytes.Length -gt 24 -and [BitConverter]::ToString($bytes, 0, 8) -eq '89-50-4E-47-0D-0A-1A-0A') 'Screenshot bytes are not PNG.'
        $imageStream = [IO.MemoryStream]::new($bytes, $false)
        $image = [Drawing.Image]::FromStream($imageStream, $true, $true)
        Assert-Smoke ($parts[$index].text -match '尺寸 (\d+)×(\d+)') 'Missing physical monitor dimensions.'
        Assert-Smoke ($image.Width -eq [int]$Matches[1] -and $image.Height -eq [int]$Matches[2]) 'PNG dimensions disagree with monitor metadata.'
        $report.monitors += @{ width = $image.Width; height = $image.Height; pngBytes = $bytes.Length }
        $image.Dispose()
        $imageStream.Dispose()
        $bytes = $null
        $dataUrl = $null
      }
      Send-Json $context @{ choices = @(@{ finish_reason = 'stop'; message = @{ role = 'assistant'; content = '本地屏幕捕获和 PNG 编码验证通过。' } }) }
    } elseif ($report.posts -eq 2) {
      Assert-Smoke ($payload.model -eq 'smoke-fairy') 'Second request must use the dedicated fairy model.'
      Assert-Smoke ($payload.messages[1].content.Contains('PNG 编码验证通过')) 'Vision description was not forwarded to fairy.'
      $calls = @(
        (Make-Tool 'local-web' 'http_get' @{ url = "http://127.0.0.1:$Port/knowledge" }),
        (Make-Tool 'local-memory' 'file_write' @{ path = 'integration/result.md'; content = '真实平台集成测试通过。' }),
        (Make-Tool 'local-speech' 'speak' @{ text = '本地平台集成测试通过。' })
      )
      Send-Json $context @{ choices = @(@{ finish_reason = 'tool_calls'; message = @{ role = 'assistant'; content = $null; tool_calls = $calls } }) }
    } else {
      Assert-Smoke ($payload.model -eq 'smoke-fairy') 'Tool follow-up must use the fairy model.'
      $replies = @($payload.messages | Where-Object role -eq 'tool')
      Assert-Smoke ($replies.Count -eq 3) 'Missing tool replies.'
      foreach ($reply in $replies) {
        $result = $reply.content | ConvertFrom-Json
        Assert-Smoke ($result.ok -eq $true) 'A real platform tool returned an error.'
        if ($reply.tool_call_id -eq 'local-web') {
          Assert-Smoke ($result.status -eq 200 -and $result.content.Contains('本地知识测试通过')) 'HTTP GET lost UTF-8 data or status.'
        }
      }
      $report.toolReplies = $replies.Count
      Send-Json $context @{ choices = @(@{ finish_reason = 'stop'; message = @{ role = 'assistant'; content = '' } }) }
    }
    $payload = $null
  }
  Assert-Smoke ($report.anonymousGets -eq 1) 'Expected exactly one anonymous GET.'
  $memory = [IO.File]::ReadAllText((Join-Path $FixtureRoot 'memory/integration/result.md'), [Text.Encoding]::UTF8)
  Assert-Smoke ($memory -eq '真实平台集成测试通过。') 'Memory content did not persist correctly.'
  $report.passed = $true
} catch {
  # Never include a request, image, or object dump in diagnostics.
  if ($_.Exception.Message.StartsWith('Smoke assertion: ')) {
    $report.error = $_.Exception.Message
  } else {
    $report.error = 'Fixture error at line ' + $_.InvocationInfo.ScriptLineNumber + ': ' + $_.Exception.GetType().Name
  }
  if ($context -and $context.Response) {
    try { $context.Response.StatusCode = 500; $context.Response.Close() } catch { }
  }
} finally {
  $listener.Stop()
  $listener.Close()
  [IO.File]::WriteAllText((Join-Path $FixtureRoot 'report.json'), ($report | ConvertTo-Json -Depth 8))
}
