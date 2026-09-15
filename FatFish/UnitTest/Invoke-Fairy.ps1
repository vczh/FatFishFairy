#Requires -Version 7.0
param(
  [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
  [ValidateSet('x64', 'Win32')][string]$Platform = 'x64',
  [string]$ScreenshotPath = ''
)

# Opt-in native GUI integration test. Uses an isolated executable, synthetic
# themes/prompts/credentials and a loopback model server. Model screen captures
# are sent only to that server and never saved; real credentials are not read.
# ScreenshotPath optionally saves native UI images, visible speech, and progress crops.
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$outputFolder = if ($Platform -eq 'x64') { "FatFish/x64/$Configuration" } else { "FatFish/$Configuration" }
$executable = Join-Path $repository "$outputFolder/FatFishFairy.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw "Build $Configuration $Platform FatFishFairy before running this test." }

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
if (-not ('FatFishFairySmoke.Native' -as [type])) {
  $drawingReferences = @([System.Drawing.Bitmap].Assembly.Location, [System.Drawing.Color].Assembly.Location)
  foreach ($reference in @('ref/System.Runtime.dll', 'ref/System.Collections.dll', 'ref/System.Security.Cryptography.dll', 'ref/System.Security.Cryptography.Algorithms.dll', 'System.Private.Windows.GdiPlus.dll', 'System.Private.Windows.Core.dll')) {
    $referencePath = Join-Path $PSHOME $reference
    if (Test-Path -LiteralPath $referencePath) { $drawingReferences += $referencePath }
  }
  Add-Type -ReferencedAssemblies $drawingReferences -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace FatFishFairySmoke
{
    public class Window
    {
        public IntPtr Handle;
        public IntPtr Owner;
        public string Title;
        public string ClassName;
        public int X, Y, Width, Height, ClientWidth, ClientHeight;
        public long Style, ExtendedStyle;
        public bool LayeredAttributesAvailable;
        public uint ColorKey, LayeredFlags;
    }

    public class Frame
    {
        public string Hash;
        public int Colors;
        public int OpaquePixels;
        public int ClickX, ClickY;
        public int ThemeColor, StageColor;
    }

    public class ProgressFrame
    {
        public string Hash;
        public int SkyBluePixels, Left, Top, Right, Bottom, BottomGap;
    }

    public static class Native
    {
        private delegate bool EnumProc(IntPtr window, IntPtr argument);
        [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr argument);
        [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr argument);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
        [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr window, uint command);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int capacity);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr window, StringBuilder text, int capacity);
        [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
        [DllImport("user32.dll")] private static extern int GetWindowRgn(IntPtr window, IntPtr region);
        [DllImport("gdi32.dll")] private static extern IntPtr CreateRectRgn(int left, int top, int right, int bottom);
        [DllImport("gdi32.dll")] private static extern bool PtInRegion(IntPtr region, int x, int y);
        [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr value);
        [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr window, out Rect rect);
        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr64(IntPtr window, int index);
        [DllImport("user32.dll", EntryPoint = "GetWindowLongW")] private static extern int GetWindowLong32(IntPtr window, int index);
        [DllImport("user32.dll")] private static extern bool GetLayeredWindowAttributes(IntPtr window, out uint color, out byte alpha, out uint flags);
        [DllImport("user32.dll")] private static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
        [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr SendMessageTimeout(IntPtr window, uint message, IntPtr wParam, StringBuilder text, uint flags, uint timeout, out IntPtr result);
        [DllImport("user32.dll", SetLastError = true)] public static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
        [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
        [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
        [DllImport("user32.dll", SetLastError = true)] public static extern bool SetCursorPos(int x, int y);
        [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
        [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);

        private static string Text(IntPtr window, bool className)
        {
            var text = new StringBuilder(2048);
            if (className) GetClassName(window, text, text.Capacity); else GetWindowText(window, text, text.Capacity);
            return text.ToString();
        }

        public static string ControlText(IntPtr window)
        {
            var text = new StringBuilder(Math.Max(2048, checked((int)SendMessage(window, 0x000E, IntPtr.Zero, IntPtr.Zero)) + 1));
            IntPtr result;
            // WM_GETTEXT is marshalled across processes by Windows; tooltip-private messages are not.
            if (SendMessageTimeout(window, 0x000D, new IntPtr(text.Capacity), text, 2, 1000, out result) == IntPtr.Zero)
                throw new InvalidOperationException("Cannot read the talking balloon text.");
            return text.ToString();
        }

        public static IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam)
        {
            IntPtr result;
            if (SendMessageTimeout(window, message, wParam, lParam, 2, 2000, out result) == IntPtr.Zero)
                throw new InvalidOperationException("The fairy UI did not respond within two seconds.");
            return result;
        }

        public static Point DownwardStem(Window balloon)
        {
            IntPtr region = CreateRectRgn(0, 0, 0, 0);
            if (region == IntPtr.Zero) throw new InvalidOperationException("Cannot allocate a balloon region.");
            try
            {
                if (GetWindowRgn(balloon.Handle, region) <= 1)
                    throw new InvalidOperationException("Cannot inspect the native balloon shape.");
                for (int y = balloon.Height - 1; y >= 0; y--)
                {
                    int left = balloon.Width, right = -1;
                    for (int x = 0; x < balloon.Width; x++)
                        if (PtInRegion(region, x, y)) { left = Math.Min(left, x); right = x; }
                    if (right >= left)
                    {
                        // A stem has fixed native height even when the text body
                        // spans many lines. Ratios of total height cannot locate it.
                        int bodyWidth = 0;
                        for (int x = 0; x < balloon.Width; x++)
                            if (PtInRegion(region, x, Math.Max(0, y - 32))) bodyWidth++;
                        if (right - left > 8 || bodyWidth * 2 < balloon.Width)
                            throw new InvalidOperationException($"Expected a wide balloon body above a narrow downward stem; body/tip widths are {bodyWidth}/{right - left + 1} in {balloon.Width}x{balloon.Height}.");
                        return new Point { X = balloon.X + (left + right) / 2, Y = balloon.Y + y };
                    }
                }
                throw new InvalidOperationException("The greeting balloon has no bottom stem tip.");
            }
            finally { DeleteObject(region); }
        }

        public static Window Describe(IntPtr handle)
        {
            Rect bounds, client;
            GetWindowRect(handle, out bounds);
            GetClientRect(handle, out client);
            byte alpha;
            uint key, flags;
            bool attributes = GetLayeredWindowAttributes(handle, out key, out alpha, out flags);
            return new Window {
                Handle = handle, Owner = GetWindow(handle, 4), Title = Text(handle, false), ClassName = Text(handle, true),
                X = bounds.Left, Y = bounds.Top, Width = bounds.Right - bounds.Left, Height = bounds.Bottom - bounds.Top,
                ClientWidth = client.Right - client.Left, ClientHeight = client.Bottom - client.Top,
                Style = IntPtr.Size == 8 ? GetWindowLongPtr64(handle, -16).ToInt64() : GetWindowLong32(handle, -16),
                ExtendedStyle = IntPtr.Size == 8 ? GetWindowLongPtr64(handle, -20).ToInt64() : GetWindowLong32(handle, -20),
                LayeredAttributesAvailable = attributes, ColorKey = key, LayeredFlags = flags
            };
        }

        public static Window[] Windows(int processId)
        {
            var result = new List<Window>();
            EnumWindows((handle, _) => {
                uint owner;
                GetWindowThreadProcessId(handle, out owner);
                if (owner == processId && IsWindowVisible(handle)) result.Add(Describe(handle));
                return true;
            }, IntPtr.Zero);
            return result.ToArray();
        }

        public static string Children(IntPtr parent)
        {
            var result = new StringBuilder();
            EnumChildWindows(parent, (handle, _) => { result.AppendLine(Text(handle, true) + ": " + Text(handle, false)); return true; }, IntPtr.Zero);
            return result.ToString();
        }

        public static IntPtr Location(int x, int y) { return new IntPtr((y << 16) | (x & 65535)); }

        public static void CreateThemeFrame(string path, int themeColor, int stage)
        {
            // Synthetic frames keep the test independent of supplied artwork. Their
            // opaque centers identify the theme and first frame without image matching.
            using (var bitmap = new Bitmap(384, 384, PixelFormat.Format32bppArgb))
            {
                for (int y = 48; y < 336; y++)
                for (int x = 64; x < 320; x++)
                    bitmap.SetPixel(x, y, Color.FromArgb(255, (x + stage * 37) % 256, y % 256, (x + y) % 256));
                int stageColor = stage == 1 ? 0xFFFFFF : stage == 2 ? 0x808080 : 0x202020;
                for (int y = 112; y < 144; y++)
                for (int x = 176; x < 208; x++)
                    bitmap.SetPixel(x, y, Color.FromArgb(unchecked((int)0xFF000000) | stageColor));
                for (int y = 168; y < 216; y++)
                for (int x = 168; x < 216; x++)
                    bitmap.SetPixel(x, y, Color.FromArgb(unchecked((int)0xFF000000) | themeColor));
                bitmap.Save(path, ImageFormat.Png);
            }
        }

        public static Frame Capture(IntPtr handle, string path)
        {
            var window = Describe(handle);
            using (var bitmap = new Bitmap(window.Width, window.Height, PixelFormat.Format32bppArgb))
            {
                using (var graphics = Graphics.FromImage(bitmap))
                {
                    IntPtr dc = graphics.GetHdc();
                    bool captured;
                    try { captured = PrintWindow(handle, dc, 2); }
                    finally { graphics.ReleaseHdc(dc); }
                    if (!captured) throw new InvalidOperationException("PrintWindow failed for the fairy window.");
                }
                var colors = new HashSet<int>();
                int opaque = 0, clickX = -1, clickY = -1;
                long bestDistance = long.MaxValue;
                for (int y = 0; y < bitmap.Height; y++)
                for (int x = 0; x < bitmap.Width; x++)
                {
                    int rgb = bitmap.GetPixel(x, y).ToArgb() & 0xFFFFFF;
                    colors.Add(rgb);
                    if (rgb != 0x00FF00 && rgb != 0)
                    {
                        opaque++;
                        long dx = x - bitmap.Width / 2, dy = y - bitmap.Height / 2;
                        long distance = dx * dx + dy * dy;
                        if (distance < bestDistance) { bestDistance = distance; clickX = x; clickY = y; }
                    }
                }
                using (var stream = new MemoryStream())
                {
                    bitmap.Save(stream, ImageFormat.Png);
                    if (!String.IsNullOrEmpty(path)) File.WriteAllBytes(path, stream.ToArray());
                    using (var hash = SHA256.Create())
                    return new Frame {
                        Hash = BitConverter.ToString(hash.ComputeHash(stream.ToArray())), Colors = colors.Count,
                        OpaquePixels = opaque, ClickX = clickX, ClickY = clickY,
                        ThemeColor = bitmap.GetPixel(bitmap.Width / 2, bitmap.Height / 2).ToArgb() & 0xFFFFFF,
                        StageColor = bitmap.GetPixel(bitmap.Width / 2, bitmap.Height / 3).ToArgb() & 0xFFFFFF
                    };
                }
            }
        }

        public static ProgressFrame CaptureProgress(IntPtr handle, string path)
        {
            var window = Describe(handle);
            int width = Math.Min(96, window.Width), height = Math.Min(48, window.Height);
            using (var bitmap = new Bitmap(window.Width, window.Height, PixelFormat.Format32bppArgb))
            {
                using (var graphics = Graphics.FromImage(bitmap))
                {
                    IntPtr dc = graphics.GetHdc();
                    bool captured;
                    try { captured = PrintWindow(handle, dc, 2); }
                    finally { graphics.ReleaseHdc(dc); }
                    if (!captured) throw new InvalidOperationException("Cannot capture the progress indicator.");
                }
                var mask = new byte[width * height];
                var frame = new ProgressFrame { Left = width, Top = window.Height, Right = -1, Bottom = -1 };
                for (int y = window.Height - height; y < window.Height; y++)
                for (int x = 0; x < width; x++)
                {
                    if ((bitmap.GetPixel(x, y).ToArgb() & 0xFFFFFF) != 0x87CEEB) continue;
                    frame.SkyBluePixels++;
                    frame.Left = Math.Min(frame.Left, x); frame.Right = Math.Max(frame.Right, x);
                    frame.Top = Math.Min(frame.Top, y); frame.Bottom = Math.Max(frame.Bottom, y);
                    mask[(y - (window.Height - height)) * width + x] = 1;
                }
                frame.BottomGap = window.Height - frame.Bottom - 1;
                if (frame.SkyBluePixels < 4 || frame.Left < 3 || frame.Left > 24
                    || frame.BottomGap < 2 || frame.BottomGap > 24 || frame.Bottom - frame.Top < 5)
                    throw new InvalidOperationException("Expected skyblue progress text inset near the fairy window's lower-left corner.");
                using (var hash = SHA256.Create()) frame.Hash = BitConverter.ToString(hash.ComputeHash(mask));
                if (!String.IsNullOrEmpty(path))
                {
                    // Save the actual screen crop for visual inspection of the glyphs.
                    using (var crop = new Bitmap(width, height, PixelFormat.Format32bppArgb))
                    {
                        using (var graphics = Graphics.FromImage(crop))
                            graphics.CopyFromScreen(window.X, window.Y + window.Height - height, 0, 0, crop.Size);
                        crop.Save(path, ImageFormat.Png);
                    }
                }
                return frame;
            }
        }

        public static void CaptureDesktop(IntPtr handle, string path)
        {
            var window = Describe(handle);
            using (var bitmap = new Bitmap(window.Width, window.Height, PixelFormat.Format32bppArgb))
            {
                using (var graphics = Graphics.FromImage(bitmap))
                    graphics.CopyFromScreen(window.X, window.Y, 0, 0, bitmap.Size);
                bitmap.Save(path, ImageFormat.Png);
            }
        }
    }
}
'@
}

[void][FatFishFairySmoke.Native]::SetProcessDpiAwarenessContext([IntPtr](-4))
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('FatFishFairySmoke-' + [Guid]::NewGuid().ToString('N'))
$application = $null
$server = $null
$serverRun = 0
$serverRoot = $null
$testFailure = $null
$progressHashes = @{}
$cursor = [FatFishFairySmoke.Native+Point]::new()
[void][FatFishFairySmoke.Native]::GetCursorPos([ref]$cursor)

function Stop-FixtureServer {
  if ($null -ne $script:server) {
    if (-not $script:server.HasExited) { $script:server.Kill(); [void]$script:server.WaitForExit(5000) }
    $script:server.Dispose()
    $script:server = $null
  }
}

function Assert-FixtureServer {
  $errorPath = Join-Path $script:serverRoot 'error.txt'
  if (Test-Path -LiteralPath $errorPath) { throw ([IO.File]::ReadAllText($errorPath)) }
  if ($script:server.HasExited) { throw 'The local desktop model fixture exited unexpectedly.' }
}

function Start-FixtureServer([string]$firstCharacter = '# 中文桌面角色 theme_a') {
  Stop-FixtureServer
  $script:serverRun++
  $script:serverRoot = Join-Path $fixture "server-$script:serverRun"
  [void][IO.Directory]::CreateDirectory($script:serverRoot)
  $speechData.firstCharacter = $firstCharacter
  [IO.File]::WriteAllText((Join-Path $script:serverRoot 'data.json'), ($speechData | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
  $probe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
  $probe.Start()
  $port = $probe.LocalEndpoint.Port
  $probe.Stop()
  $modelConfiguration = @{ apikey = 'synthetic-fairy-key'; url = "http://127.0.0.1:$port/v1"; auth_header = 'Authorization: Bearer $APIKEY'; vision_model = 'desktop-vision'; fairy_model = 'desktop-fairy' }
  [IO.File]::WriteAllText((Join-Path $fixture 'env/apikey.json'), ($modelConfiguration | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
  $shell = (Get-Process -Id $PID).Path
  $script:server = Start-Process -FilePath $shell -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile', '-File', ('"' + (Join-Path $PSScriptRoot 'Server-Fairy.ps1') + '"'), '-FixtureRoot', ('"' + $script:serverRoot + '"'), '-Port', $port)
  for ($attempt = 0; $attempt -lt 100; $attempt++) {
    Assert-FixtureServer
    if (Test-Path -LiteralPath (Join-Path $script:serverRoot 'ready')) { return }
    Start-Sleep -Milliseconds 100
  }
  throw 'The local desktop model fixture did not become ready.'
}

function Wait-ModelRequest([string]$sequence) {
  for ($attempt = 0; $attempt -lt 200; $attempt++) {
    Assert-FixtureServer
    [void](Get-TestWindows)
    if (Test-Path -LiteralPath (Join-Path $script:serverRoot "pending-$sequence")) { return }
    Start-Sleep -Milliseconds 100
  }
  $windows = @(Get-TestWindows)
  $bubble = $windows | Where-Object ClassName -eq 'tooltips_class32' | Select-Object -First 1
  $bubbleText = if ($null -ne $bubble) { [FatFishFairySmoke.Native]::ControlText($bubble.Handle) } else { '<hidden>' }
  if (-not [string]::IsNullOrEmpty($ScreenshotPath)) {
    $main = $windows | Where-Object { $_.ClassName -eq 'VczhWindow' -and $_.Width -ge 300 -and $_.Height -ge 300 } | Select-Object -First 1
    if ($null -ne $main) { [FatFishFairySmoke.Native]::CaptureDesktop($main.Handle, [IO.Path]::ChangeExtension($ScreenshotPath, 'pending-failure.png')) }
  }
  throw "The continuous agent loop did not reach model request $sequence. Current fixture bubble: $bubbleText"
}

function Assert-Progress($mainWindow, [string]$expected, [string]$captureName = '') {
  $lastError = ''
  for ($attempt = 0; $attempt -lt 50; $attempt++) {
    [void](Get-TestWindows)
    try {
      $frame = [FatFishFairySmoke.Native]::CaptureProgress($mainWindow.Handle, '')
      $matches = if ($progressHashes.ContainsKey($expected)) { $frame.Hash -ceq $progressHashes[$expected] } else { $progressHashes.Values -cnotcontains $frame.Hash }
      if ($matches) {
        $progressHashes[$expected] = $frame.Hash
        if (-not [string]::IsNullOrEmpty($ScreenshotPath)) {
          $name = if ($captureName.Length -gt 0) { $captureName } else { $expected }
          [void][FatFishFairySmoke.Native]::CaptureProgress($mainWindow.Handle, [IO.Path]::ChangeExtension($ScreenshotPath, "progress-$name.png"))
        }
        return
      }
      $lastError = 'The rendered glyphs still match another progress state.'
    } catch {
      $lastError = $_.Exception.Message
    }
    Start-Sleep -Milliseconds 100
  }
  throw "The progress indicator did not reach $expected. $lastError"
}

function Complete-ModelRequests([int]$first, [int]$last) {
  for ($sequence = $first; $sequence -le $last; $sequence++) {
    Wait-ModelRequest $sequence
    [IO.File]::WriteAllText((Join-Path $script:serverRoot "release-$sequence"), 'release')
  }
  Wait-ModelRequest ($last + 1)
}

function Wait-Bubble([string]$expectedText, [switch]$Prefix) {
  $lastText = ''
  for ($attempt = 0; $attempt -lt 100; $attempt++) {
    $balloons = @(Get-TestWindows | Where-Object ClassName -eq 'tooltips_class32')
    if ($expectedText.Length -eq 0) {
      if ($balloons.Count -eq 0) { return }
    } elseif ($balloons.Count -eq 1) {
      $lastText = [FatFishFairySmoke.Native]::ControlText($balloons[0].Handle).Replace("`r", '')
      if (($Prefix -and $lastText.StartsWith($expectedText, [StringComparison]::Ordinal)) -or (-not $Prefix -and $lastText -ceq $expectedText)) { return $balloons[0] }
    }
    Start-Sleep -Milliseconds 100
  }
  throw "The talking balloon did not display the expected result (expected length $($expectedText.Length), actual length $($lastText.Length))."
}

function Read-SpeechHistory {
  if (-not (Test-Path -LiteralPath $historyPath)) { return '' }
  return [Text.UTF8Encoding]::new($false, $true).GetString([IO.File]::ReadAllBytes($historyPath)).TrimStart([char]0xFEFF)
}

function Assert-SpeechHistory([string]$expected) {
  if ((Read-SpeechHistory) -cne $expected) { throw 'Speech history changed before a nonempty fairy round completed.' }
}

function Assert-SpeechHistoryAppend([string]$previous, [string]$speech, [datetime]$earliest) {
  $history = Read-SpeechHistory
  $pattern = '\A' + [regex]::Escape($previous) + '# Speak (?<timestamp>[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}-[0-9]{2}-[0-9]{2})\n\n' + [regex]::Escape($speech) + '\n\n\z'
  $entry = [regex]::Match($history, $pattern)
  if (-not $entry.Success) { throw 'Speech history did not append exactly one UTF-8 Markdown entry containing the complete fairy speech.' }
  $timestamp = [datetime]::ParseExact($entry.Groups['timestamp'].Value, 'yyyy-MM-dd HH-mm-ss', [Globalization.CultureInfo]::InvariantCulture)
  if ($timestamp -lt $earliest.AddSeconds(-1) -or $timestamp -gt [datetime]::Now) {
    throw 'Speech history must timestamp completed speech using the current local date and time.'
  }
  return $history
}

function Assert-BubblePlacement($mainWindow, $balloon) {
  $workArea = [Windows.Forms.Screen]::FromHandle($mainWindow.Handle).WorkingArea
  if ($balloon.X -lt $workArea.Left -or $balloon.Y -lt $workArea.Top -or ($balloon.X + $balloon.Width) -gt $workArea.Right -or ($balloon.Y + $balloon.Height) -gt $workArea.Bottom) {
    throw 'The updated talking balloon did not stay within the monitor work area.'
  }
  $expectedY = [Math]::Max($workArea.Top, $mainWindow.Y - $balloon.Height)
  if ([Math]::Abs($balloon.Y - $expectedY) -gt 2) { throw 'The updated talking balloon did not stay just above the fairy or clamp to the work area.' }
  $stem = [FatFishFairySmoke.Native]::DownwardStem($balloon)
  if ([Math]::Abs($stem.X - ($mainWindow.X + $mainWindow.Width / 2)) -gt 4) { throw 'The resized talking balloon lost its horizontal stem target.' }
}

function Get-TestWindows([switch]$AllowExit) {
  if ($application.HasExited) {
    if ($AllowExit) { return @() }
    throw "FatFishFairy exited unexpectedly with code $($application.ExitCode)."
  }
  $windows = @([FatFishFairySmoke.Native]::Windows($application.Id))
  $crash = $windows | Where-Object { $_.Title -eq 'Microsoft Visual C++ Runtime Library' -or $_.ClassName -eq '#32770' } | Select-Object -First 1
  if ($null -ne $crash) {
    $details = $crash.Title + "`n" + [FatFishFairySmoke.Native]::Children($crash.Handle)
    [void][FatFishFairySmoke.Native]::PostMessage($crash.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    throw "Native dialog blocked FatFishFairy: $details"
  }
  return $windows
}

function Wait-MainWindow {
  for ($attempt = 0; $attempt -lt 100; $attempt++) {
    $windows = @(Get-TestWindows | Where-Object { $_.ClassName -eq 'VczhWindow' -and $_.Width -ge 300 -and $_.Height -ge 300 })
    if ($windows.Count -eq 1) { return $windows[0] }
    Start-Sleep -Milliseconds 100
  }
  throw 'FatFishFairy did not show exactly one main window.'
}

function Assert-Window($window, [int]$expectedX, [int]$expectedY) {
  if ($window.Width -ne 384 -or $window.Height -ne 384 -or $window.ClientWidth -ne 384 -or $window.ClientHeight -ne 384) {
    throw "Expected 384x384 outer and client bounds, received $($window.Width)x$($window.Height), client $($window.ClientWidth)x$($window.ClientHeight)."
  }
  if (($window.ExtendedStyle -band 0x80008) -ne 0x80008) { throw 'The fairy must remain topmost and layered.' }
  if (-not $window.LayeredAttributesAvailable -or ($window.LayeredFlags -band 1) -eq 0 -or $window.ColorKey -ne 0x00FF00) { throw 'The fairy must use the green transparency color key.' }
  if ($window.X -ne $expectedX -or $window.Y -ne $expectedY) { throw "Expected position ($expectedX, $expectedY), received ($($window.X), $($window.Y))." }
}

function Assert-Greeting($mainWindow) {
  $balloon = $null
  for ($attempt = 0; $attempt -lt 50; $attempt++) {
    $balloons = @(Get-TestWindows | Where-Object { $_.ClassName -eq 'tooltips_class32' })
    if ($balloons.Count -eq 1) { $balloon = $balloons[0]; break }
    Start-Sleep -Milliseconds 100
  }
  if ($null -eq $balloon) { throw 'Expected exactly one visible native greeting balloon.' }
  if ([FatFishFairySmoke.Native]::ControlText($balloon.Handle) -cne 'Hello, world!') { throw 'The native greeting balloon did not display Hello, world!.' }
  if (($balloon.Style -band 0x40) -eq 0) { throw 'The greeting tooltip must use the system balloon style.' }
  if (($balloon.ExtendedStyle -band 8) -eq 0 -or $balloon.Owner -ne $mainWindow.Handle) { throw 'The greeting balloon must be topmost and owned by the fairy window.' }
  $gap = $mainWindow.Y - ($balloon.Y + $balloon.Height)
  if ($gap -lt -2 -or $gap -gt 24 -or $balloon.Width -le 0 -or $balloon.Height -le 0) {
    throw "The greeting balloon must sit just above the fairy: balloon ($($balloon.X), $($balloon.Y), $($balloon.Width), $($balloon.Height)), fairy ($($mainWindow.X), $($mainWindow.Y))."
  }
  if ($balloon.X -ge ($mainWindow.X + $mainWindow.Width) -or ($balloon.X + $balloon.Width) -le $mainWindow.X) { throw 'The greeting balloon must overlap the fairy horizontally.' }
  $stem = [FatFishFairySmoke.Native]::DownwardStem($balloon)
  $targetX = $mainWindow.X + $mainWindow.Width / 2
  $stemGap = $mainWindow.Y - $stem.Y
  if ([Math]::Abs($stem.X - $targetX) -gt 4 -or $stemGap -lt -2 -or $stemGap -gt 24) {
    throw "The downward balloon tip must point at the fairy's top center: tip ($($stem.X), $($stem.Y)), target ($targetX, $($mainWindow.Y))."
  }
  return $balloon
}

function Assert-GreetingMoved($beforeMain, $beforeBalloon, $afterMain) {
  $afterBalloon = Assert-Greeting $afterMain
  if (($afterBalloon.X - $beforeBalloon.X) -ne ($afterMain.X - $beforeMain.X) -or ($afterBalloon.Y - $beforeBalloon.Y) -ne ($afterMain.Y - $beforeMain.Y)) {
    throw 'The greeting balloon did not follow the fairy window movement.'
  }
  return $afterBalloon
}

function Open-ContextMenu($mainWindow) {
  $location = [FatFishFairySmoke.Native]::Location(192, 192)
  [void][FatFishFairySmoke.Native]::SendMessage($mainWindow.Handle, 0x0204, [IntPtr]2, $location)
  [void][FatFishFairySmoke.Native]::SendMessage($mainWindow.Handle, 0x0205, [IntPtr]::Zero, $location)
  $menu = $null
  for ($attempt = 0; $attempt -lt 50; $attempt++) {
    $popups = @(Get-TestWindows | Where-Object { $_.Handle -ne $mainWindow.Handle -and $_.ClassName -eq $mainWindow.ClassName -and $_.Width -gt 0 -and $_.Height -gt 0 })
    if ($popups.Count -eq 1) { $menu = $popups[0]; break }
    Start-Sleep -Milliseconds 100
  }
  if ($null -eq $menu) { throw 'Right-click did not open the context menu.' }
  return $menu
}

function Click-MenuItem($menu, [int]$index, [int]$count) {
  $menuLocation = [FatFishFairySmoke.Native]::Location([int]($menu.ClientWidth / 2), [int]($menu.ClientHeight * ($index + 0.5) / $count))
  [void][FatFishFairySmoke.Native]::SendMessage($menu.Handle, 0x0200, [IntPtr]::Zero, $menuLocation)
  [void][FatFishFairySmoke.Native]::SendMessage($menu.Handle, 0x0201, [IntPtr]1, $menuLocation)
  [void][FatFishFairySmoke.Native]::PostMessage($menu.Handle, 0x0202, [IntPtr]::Zero, $menuLocation)
}

function Open-ThemesMenu($mainWindow) {
  $contextMenu = Open-ContextMenu $mainWindow
  Click-MenuItem $contextMenu 0 2
  for ($attempt = 0; $attempt -lt 50; $attempt++) {
    $submenus = @(Get-TestWindows | Where-Object { $_.Handle -ne $mainWindow.Handle -and $_.Handle -ne $contextMenu.Handle -and $_.ClassName -eq $mainWindow.ClassName -and $_.Width -gt 0 -and $_.Height -gt 0 })
    if ($submenus.Count -eq 1) {
      if (-not [string]::IsNullOrEmpty($ScreenshotPath)) {
        Start-Sleep -Milliseconds 200
        [void][FatFishFairySmoke.Native]::Capture($contextMenu.Handle, [IO.Path]::ChangeExtension($ScreenshotPath, 'menu.png'))
        [void][FatFishFairySmoke.Native]::Capture($submenus[0].Handle, [IO.Path]::ChangeExtension($ScreenshotPath, 'themes.png'))
      }
      return $submenus[0]
    }
    Start-Sleep -Milliseconds 100
  }
  throw 'The first context-menu item did not open the theme submenu.'
}

function Assert-Theme($mainWindow, [int]$themeIndex, [switch]$FirstFrame) {
  for ($attempt = 0; $attempt -lt 10; $attempt++) {
    $frame = [FatFishFairySmoke.Native]::Capture($mainWindow.Handle, '')
    if ($frame.ThemeColor -eq $themeColors[$themeIndex]) { break }
    Start-Sleep -Milliseconds 50
  }
  if ($frame.ThemeColor -ne $themeColors[$themeIndex]) {
    throw "Expected rendered theme $($themeKeys[$themeIndex]) with marker $($themeColors[$themeIndex]), received $($frame.ThemeColor)."
  }
  if ($FirstFrame -and $frame.StageColor -ne 0xFFFFFF) { throw 'Switching the theme did not immediately display its first animation frame.' }
}

function Select-Theme($mainWindow, [int]$themeIndex) {
  $previous = [IO.File]::ReadAllText($configPath) | ConvertFrom-Json
  $submenu = Open-ThemesMenu $mainWindow
  Click-MenuItem $submenu $themeIndex $themeKeys.Count
  $selected = $null
  for ($attempt = 0; $attempt -lt 50; $attempt++) {
    [void](Get-TestWindows)
    $selected = [IO.File]::ReadAllText($configPath) | ConvertFrom-Json
    if ($selected.selectedTheme -ceq $themeKeys[$themeIndex]) { break }
    Start-Sleep -Milliseconds 100
  }
  if ($selected.selectedTheme -cne $themeKeys[$themeIndex]) { throw "Selecting theme menu row $themeIndex did not persist $($themeKeys[$themeIndex]); metadata ordering was not preserved." }
  Assert-Theme $mainWindow $themeIndex -FirstFrame
  if ($selected.windowX -ne $previous.windowX -or $selected.windowY -ne $previous.windowY -or $selected.retained.value -cne 'untouched') {
    throw 'Selecting a theme did not preserve the saved window position and unrelated configuration.'
  }
}

function Close-ThroughMenu($mainWindow) {
  $menu = Open-ContextMenu $mainWindow
  Click-MenuItem $menu 1 2
  for ($attempt = 0; $attempt -lt 50 -and -not $application.HasExited; $attempt++) {
    [void](Get-TestWindows -AllowExit)
    Start-Sleep -Milliseconds 100
  }
  if (-not $application.HasExited) { throw 'Clicking the exit menu item did not stop FatFishFairy.' }
  if ($application.ExitCode -ne 0) { throw "FatFishFairy returned exit code $($application.ExitCode)." }
}

try {
  $fixtureOutput = Join-Path $fixture $outputFolder
  [void][IO.Directory]::CreateDirectory($fixtureOutput)
  [void][IO.Directory]::CreateDirectory((Join-Path $fixture 'env'))
  [void][IO.Directory]::CreateDirectory((Join-Path $fixture 'memory'))
  Copy-Item -LiteralPath $executable -Destination (Join-Path $fixtureOutput 'FatFishFairy.exe')
  foreach ($name in @('Tools.md', 'Guidance.md', 'Request_Vision.md', 'Request_Fairy.md')) {
    [IO.File]::WriteAllText((Join-Path $fixture "env/$name"), "# 中文桌面测试 $name", [Text.UTF8Encoding]::new($false))
  }
  $speechData = @{
    visionFirst = "第一段完整观察。`n第二段完整观察。"
    longSpeech = '第一段中文长回复：' + ('桌面精灵应保留完整文字，窗口继续播放动画。' * 30)
    secondSpeech = "第二段回复。`n这一行来自同一次 speak。"
    followUpSpeech = '第三段回复来自工具反馈之后。'
    recoveredSpeech = '发生错误后已经恢复，继续观察桌面。'
    fallbackCharacter = '# 中文桌面角色 loli_maid 回退'
    secondCharacter = '# 中文桌面角色 theme_a'
  }
  $completeSpeech = $speechData.longSpeech + "`n" + $speechData.secondSpeech + "`n" + $speechData.followUpSpeech
  Start-FixtureServer '# 中文桌面角色 theme_z'
  # Deliberately use keys in nonalphabetical order and distinct Chinese display
  # names. Menu-row selection verifies order; optional screenshots show the labels.
  $themeKeys = @('theme_z', 'theme_a', 'theme_m')
  $themeColors = @(0xE01020, 0x2050E0, 0xE0C010)
  $themesFolder = Join-Path $fixture 'themes'
  [void][IO.Directory]::CreateDirectory($themesFolder)
  [IO.File]::WriteAllText((Join-Path $themesFolder 'theme.json'), '{"theme_z":"默认主题","theme_a":"第二主题","theme_m":"第三主题"}', [Text.UTF8Encoding]::new($false))
  for ($themeIndex = 0; $themeIndex -lt $themeKeys.Count; $themeIndex++) {
    $themeFolder = Join-Path $themesFolder $themeKeys[$themeIndex]
    [void][IO.Directory]::CreateDirectory($themeFolder)
    [IO.File]::WriteAllText((Join-Path $themeFolder 'index.json'), '{"sample":3}', [Text.UTF8Encoding]::new($false))
    if ($themeIndex -ne 2) {
      [IO.File]::WriteAllText((Join-Path $themeFolder 'Character.md'), ('# 中文桌面角色 ' + $themeKeys[$themeIndex]), [Text.UTF8Encoding]::new($false))
    }
    for ($stage = 1; $stage -le 3; $stage++) {
      [FatFishFairySmoke.Native]::CreateThemeFrame((Join-Path $themeFolder "sample_$stage.png"), $themeColors[$themeIndex], $stage)
    }
  }
  # The third catalog theme deliberately lacks Character.md, and loli_maid is
  # deliberately absent from the catalog: only its fallback prompt is needed.
  [void][IO.Directory]::CreateDirectory((Join-Path $themesFolder 'loli_maid'))
  [IO.File]::WriteAllText((Join-Path $themesFolder 'loli_maid/Character.md'), $speechData.fallbackCharacter, [Text.UTF8Encoding]::new($false))
  $configPath = Join-Path $fixture 'env/config.json'
  $historyPath = Join-Path $fixture 'env/history.md'
  $history = ''
  [IO.File]::WriteAllText($configPath, '{"windowX":123,"windowY":91,"retained":{"value":"untouched"}}', [Text.UTF8Encoding]::new($false))
  $application = Start-Process -FilePath (Join-Path $fixtureOutput 'FatFishFairy.exe') -WorkingDirectory $env:SystemRoot -WindowStyle Hidden -PassThru
  $main = Wait-MainWindow
  Assert-Window $main 123 91
  $greeting = Assert-Greeting $main
  Wait-ModelRequest 1
  Assert-Progress $main 'V'
  Assert-SpeechHistory $history
  Start-Sleep -Milliseconds 200
  $frame = [FatFishFairySmoke.Native]::Capture($main.Handle, $ScreenshotPath)
  if (-not [string]::IsNullOrEmpty($ScreenshotPath)) {
    [void][FatFishFairySmoke.Native]::Capture($greeting.Handle, [IO.Path]::ChangeExtension($ScreenshotPath, 'balloon.png'))
  }
  if ($frame.Colors -lt 64 -or $frame.OpaquePixels -lt 1000) { throw 'The fairy window did not render a character image.' }
  Assert-Theme $main 0
  $hashes = [Collections.Generic.HashSet[string]]::new()
  [void]$hashes.Add($frame.Hash)
  for ($sample = 0; $sample -lt 5 -and $hashes.Count -lt 2; $sample++) {
    Start-Sleep -Milliseconds 1100
    [void](Get-TestWindows)
    $frame = [FatFishFairySmoke.Native]::Capture($main.Handle, '')
    [void]$hashes.Add($frame.Hash)
  }
  if ($hashes.Count -lt 2) { throw 'Rendered animation did not advance between frame samples.' }
  Write-Output 'Window bounds, executable-relative configuration, topmost, color-key transparency and animation passed.'

  foreach ($themeIndex in @(1, 2, 0, 1)) { Select-Theme $main $themeIndex }
  Write-Output 'Missing selection defaulted to the first theme; each submenu row selected its metadata-ordered theme, immediately rendered frame 1 and preserved configuration.'

  $main = [FatFishFairySmoke.Native]::Describe($main.Handle)
  $frame = [FatFishFairySmoke.Native]::Capture($main.Handle, '')
  $click = [FatFishFairySmoke.Native+Point]::new()
  $click.X = $main.X + $frame.ClickX
  $click.Y = $main.Y + $frame.ClickY
  $hitWindow = [FatFishFairySmoke.Native]::WindowFromPoint($click)
  if ($hitWindow -ne $main.Handle) { throw "The rendered character is not mouse-targetable: window at its visible center is $hitWindow; fairy HWND is $($main.Handle)." }
  $corner = [FatFishFairySmoke.Native+Point]::new()
  $corner.X = $main.X + 1
  $corner.Y = $main.Y + 1
  if ([FatFishFairySmoke.Native]::WindowFromPoint($corner) -eq $main.Handle) { throw 'The transparent exterior intercepts mouse input.' }
  [void][FatFishFairySmoke.Native]::SetForegroundWindow($main.Handle)
  if (-not [FatFishFairySmoke.Native]::SetCursorPos($click.X, $click.Y)) { throw "Cannot position mouse on the character: Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())." }
  [FatFishFairySmoke.Native]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 150
  if (-not [FatFishFairySmoke.Native]::SetCursorPos(($click.X + 47), ($click.Y + 31))) { throw "Cannot move mouse while dragging: Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())." }
  Start-Sleep -Milliseconds 200
  $dragged = [FatFishFairySmoke.Native]::Describe($main.Handle)
  [void](Assert-GreetingMoved $main $greeting $dragged)
  [FatFishFairySmoke.Native]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 300
  [void](Get-TestWindows)
  $moved = [FatFishFairySmoke.Native]::Describe($main.Handle)
  if ($moved.X -eq $main.X -and $moved.Y -eq $main.Y) { throw 'Dragging the visible character did not move the window.' }
  $saved = [IO.File]::ReadAllText($configPath) | ConvertFrom-Json
  if ($saved.selectedTheme -cne $themeKeys[1] -or $saved.retained.value -cne 'untouched') { throw 'Dragging did not preserve the selected theme and unrelated configuration.' }
  Assert-Window $moved $saved.windowX $saved.windowY
  $movedGreeting = Assert-GreetingMoved $main $greeting $moved
  if (-not [FatFishFairySmoke.Native]::SetWindowPos($moved.Handle, [IntPtr]::Zero, ($moved.X + 29), ($moved.Y + 41), 0, 0, 0x15)) { throw "Cannot move the fairy window through SetWindowPos: Win32 error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())." }
  Start-Sleep -Milliseconds 200
  $positioned = [FatFishFairySmoke.Native]::Describe($moved.Handle)
  Assert-Window $positioned ($moved.X + 29) ($moved.Y + 41)
  [void](Assert-GreetingMoved $moved $movedGreeting $positioned)

  # The first model response has remained blocked throughout animation, dragging
  # and menu selection. Now complete each round and hold the following request,
  # making speech replacement and automatic restart deterministic to inspect.
  Complete-ModelRequests 1 2
  Assert-Progress $positioned 'F'
  Assert-SpeechHistory $history
  # Change selection while a fairy request is pending. All its tool-feedback
  # submissions must keep the character captured at the start of this round.
  Select-Theme $positioned 2
  Complete-ModelRequests 3 3
  Select-Theme $positioned 1
  [IO.File]::WriteAllText((Join-Path $script:serverRoot 'release-4'), 'release')
  Wait-ModelRequest '4-retry-1'
  Assert-Progress $positioned 'F1'
  [IO.File]::WriteAllText((Join-Path $script:serverRoot 'release-4-retry-1'), 'release')
  Wait-ModelRequest '4-retry-2'
  Assert-Progress $positioned 'F2'
  [IO.File]::WriteAllText((Join-Path $script:serverRoot 'release-4-retry-2'), 'release')
  Wait-ModelRequest 5
  Assert-SpeechHistory $history
  # Switch during the last fairy follow-up so the following round must use the
  # missing-character fallback with a new fairy conversation. The fixture
  # rejects any user, assistant or tool history left over from this round.
  Select-Theme $positioned 2
  Assert-Progress $positioned 'F2' 'F2-theme-switch'
  $speechStarted = [datetime]::Now
  Complete-ModelRequests 5 5
  Assert-Progress $positioned 'V' 'V-after-recovered-round'
  $spoken = Wait-Bubble $completeSpeech
  $history = Assert-SpeechHistoryAppend $history $completeSpeech $speechStarted
  if ($spoken.Width -le $greeting.Width -or $spoken.Height -le $greeting.Height) { throw 'The native balloon did not resize for a long multiline model response.' }
  Assert-BubblePlacement $positioned $spoken
  $workArea = [Windows.Forms.Screen]::FromHandle($positioned.Handle).WorkingArea
  $speechWindowY = [Math]::Min($workArea.Bottom - $positioned.Height, $workArea.Top + $spoken.Height + 24)
  if (-not [FatFishFairySmoke.Native]::SetWindowPos($positioned.Handle, [IntPtr]::Zero, ($positioned.X + 13), $speechWindowY, 0, 0, 0x15)) { throw 'Cannot move the fairy while verifying its resized talking balloon.' }
  Start-Sleep -Milliseconds 250
  $positioned = [FatFishFairySmoke.Native]::Describe($positioned.Handle)
  $spoken = Wait-Bubble $completeSpeech
  Assert-BubblePlacement $positioned $spoken
  if (-not [string]::IsNullOrEmpty($ScreenshotPath)) {
    # PrintWindow truncates long native-tooltip text despite WM_GETTEXT and
    # on-screen painting retaining it. Use speech-desktop.png for visual QA.
    [void][FatFishFairySmoke.Native]::Capture($spoken.Handle, [IO.Path]::ChangeExtension($ScreenshotPath, 'speech.png'))
    [FatFishFairySmoke.Native]::CaptureDesktop($spoken.Handle, [IO.Path]::ChangeExtension($ScreenshotPath, 'speech-desktop.png'))
  }
  Complete-ModelRequests 6 8
  Select-Theme $positioned 1
  Complete-ModelRequests 9 9
  Wait-Bubble ''
  Assert-SpeechHistory $history
  Complete-ModelRequests 10 10
  Assert-Progress $positioned 'V1'
  [void](Wait-Bubble '调用大模型发生错误：' -Prefix)
  Assert-SpeechHistory $history
  $speechStarted = [datetime]::Now
  Complete-ModelRequests 11 12
  Assert-Progress $positioned 'F1' 'F1-after-failed-round'
  Complete-ModelRequests 13 14
  Assert-Progress $positioned 'V' 'V-after-success'
  $recovered = Wait-Bubble $speechData.recoveredSpeech
  $history = Assert-SpeechHistoryAppend $history $speechData.recoveredSpeech $speechStarted
  Assert-BubblePlacement $positioned $recovered
  Complete-ModelRequests 15 15
  # The http_get tool's request 16 has no release file: exit must cancel it.
  # The following restart/fallback runs also exit during a pending model POST.
  Close-ThroughMenu $positioned
  Assert-SpeechHistory $history
  Assert-FixtureServer
  Write-Output 'Native greeting shape/movement, responsive UI during a pending request, complete multiline speech, empty speech, error recovery, fixed character within pending rounds, fresh fairy conversations after theme switching with fallback, and exit during a pending request passed.'
  Write-Output 'Skyblue lower-left progress glyphs changed through V, F, F1, F2 and V1, preserved the failure counter across a theme switch, and reset after successful rounds. Optional progress-*.png files show actual screen crops for text inspection.'

  $application.Dispose()
  Start-FixtureServer
  $application = Start-Process -FilePath (Join-Path $fixtureOutput 'FatFishFairy.exe') -WorkingDirectory $env:SystemRoot -WindowStyle Hidden -PassThru
  $restarted = Wait-MainWindow
  Assert-Window $restarted $saved.windowX $saved.windowY
  [void](Assert-Greeting $restarted)
  Wait-ModelRequest 1
  Assert-SpeechHistory $history
  Start-Sleep -Milliseconds 200
  Assert-Theme $restarted 1
  Complete-ModelRequests 1 2
  Close-ThroughMenu $restarted
  Assert-SpeechHistory $history
  Assert-FixtureServer
  Write-Output 'Restart restored the dragged position and selected theme, submitted its character prompt, and exited with code 0.'

  $saved.selectedTheme = 'unknown_theme'
  [IO.File]::WriteAllText($configPath, ($saved | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
  $application.Dispose()
  Start-FixtureServer '# 中文桌面角色 theme_z'
  $application = Start-Process -FilePath (Join-Path $fixtureOutput 'FatFishFairy.exe') -WorkingDirectory $env:SystemRoot -WindowStyle Hidden -PassThru
  $fallback = Wait-MainWindow
  Assert-Window $fallback $saved.windowX $saved.windowY
  [void](Assert-Greeting $fallback)
  Wait-ModelRequest 1
  Start-Sleep -Milliseconds 200
  Assert-Theme $fallback 0
  Complete-ModelRequests 1 2
  Close-ThroughMenu $fallback
  Assert-SpeechHistory $history
  Assert-FixtureServer
  Write-Output 'Unknown selectedTheme defaulted to the first theme and its character prompt; final menu exit completed with code 0.'

  Stop-FixtureServer
  Remove-Item -LiteralPath (Join-Path $fixture 'env/apikey.json')
  $application.Dispose()
  $application = Start-Process -FilePath (Join-Path $fixtureOutput 'FatFishFairy.exe') -WorkingDirectory $env:SystemRoot -WindowStyle Hidden -PassThru
  $unconfigured = Wait-MainWindow
  [void](Wait-Bubble '调用大模型发生错误：' -Prefix)
  Close-ThroughMenu $unconfigured
  Assert-SpeechHistory $history
  Write-Output 'Missing model configuration appeared in the error balloon and the desktop remained usable.'
  Write-Output 'UTF-8 speech history recorded complete nonempty fairy rounds with local timestamps, preserved prior entries across restarts, and excluded greetings, observations, partial rounds, silence and errors.'
} catch {
  $testFailure = $_
  try {
    $windows = @(Get-TestWindows -AllowExit)
    foreach ($bubble in @($windows | Where-Object ClassName -eq 'tooltips_class32')) {
      Write-Output ('Fixture failure bubble: ' + [FatFishFairySmoke.Native]::ControlText($bubble.Handle))
    }
    if (-not [string]::IsNullOrEmpty($ScreenshotPath)) {
      $main = $windows | Where-Object { $_.ClassName -eq 'VczhWindow' -and $_.Width -ge 300 -and $_.Height -ge 300 } | Select-Object -First 1
      if ($null -ne $main) { [FatFishFairySmoke.Native]::CaptureDesktop($main.Handle, [IO.Path]::ChangeExtension($ScreenshotPath, 'failure.png')) }
    }
  } catch { }
  throw
} finally {
  [FatFishFairySmoke.Native]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
  [void][FatFishFairySmoke.Native]::SetCursorPos($cursor.X, $cursor.Y)
  if ($null -ne $application -and -not $application.HasExited) { $application.Kill(); [void]$application.WaitForExit(5000) }
  if ($null -ne $application) { $application.Dispose() }
  Stop-FixtureServer
  $fixtureFull = [IO.Path]::GetFullPath($fixture)
  $temporaryPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
  if (-not $fixtureFull.StartsWith($temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or -not [IO.Path]::GetFileName($fixtureFull).StartsWith('FatFishFairySmoke-')) { throw 'Unsafe fairy fixture cleanup path.' }
  for ($attempt = 0; $attempt -lt 20 -and (Test-Path -LiteralPath $fixtureFull); $attempt++) {
    try {
      Remove-Item -LiteralPath $fixtureFull -Recurse -Force
    } catch {
      if ($attempt -eq 19) {
        if ($null -eq $testFailure) { throw }
        Write-Warning "The GUI test failed and its temporary fixture could not yet be removed: $fixtureFull"
      } else {
        # Windows may briefly retain the mapped executable after process exit.
        Start-Sleep -Milliseconds 100
      }
    }
  }
}
