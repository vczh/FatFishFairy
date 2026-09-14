#Requires -Version 7.0
param(
  [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
  [ValidateSet('x64', 'Win32')][string]$Platform = 'x64',
  [string]$ScreenshotPath = ''
)

# Opt-in native GUI integration test. Only a temporary copy of the executable,
# theme assets and position configuration is used; credentials are never copied.
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$outputFolder = if ($Platform -eq 'x64') { "FatFish/x64/$Configuration" } else { "FatFish/$Configuration" }
$executable = Join-Path $repository "$outputFolder/FatFishFairy.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw "Build $Configuration $Platform FatFishFairy before running this test." }

Add-Type -AssemblyName System.Drawing
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
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int capacity);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr window, StringBuilder text, int capacity);
        [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr window, out Rect rect);
        [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr window, out Rect rect);
        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr64(IntPtr window, int index);
        [DllImport("user32.dll", EntryPoint = "GetWindowLongW")] private static extern int GetWindowLong32(IntPtr window, int index);
        [DllImport("user32.dll")] private static extern bool GetLayeredWindowAttributes(IntPtr window, out uint color, out byte alpha, out uint flags);
        [DllImport("user32.dll")] private static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
        [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
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

        public static Window Describe(IntPtr handle)
        {
            Rect bounds, client;
            GetWindowRect(handle, out bounds);
            GetClientRect(handle, out client);
            byte alpha;
            uint key, flags;
            bool attributes = GetLayeredWindowAttributes(handle, out key, out alpha, out flags);
            return new Window {
                Handle = handle, Title = Text(handle, false), ClassName = Text(handle, true),
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
                    return new Frame { Hash = BitConverter.ToString(hash.ComputeHash(stream.ToArray())), Colors = colors.Count, OpaquePixels = opaque, ClickX = clickX, ClickY = clickY };
                }
            }
        }
    }
}
'@
}

[void][FatFishFairySmoke.Native]::SetProcessDpiAwarenessContext([IntPtr](-4))
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('FatFishFairySmoke-' + [Guid]::NewGuid().ToString('N'))
$application = $null
$testFailure = $null
$cursor = [FatFishFairySmoke.Native+Point]::new()
[void][FatFishFairySmoke.Native]::GetCursorPos([ref]$cursor)

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
    $windows = @(Get-TestWindows | Where-Object { $_.Width -ge 300 -and $_.Height -ge 300 })
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

function Close-ThroughMenu($mainWindow) {
  $location = [FatFishFairySmoke.Native]::Location(192, 192)
  [void][FatFishFairySmoke.Native]::SendMessage($mainWindow.Handle, 0x0204, [IntPtr]2, $location)
  [void][FatFishFairySmoke.Native]::SendMessage($mainWindow.Handle, 0x0205, [IntPtr]::Zero, $location)
  $menu = $null
  for ($attempt = 0; $attempt -lt 50; $attempt++) {
    $popups = @(Get-TestWindows | Where-Object { $_.Handle -ne $mainWindow.Handle -and $_.Width -gt 0 -and $_.Height -gt 0 })
    if ($popups.Count -eq 1) { $menu = $popups[0]; break }
    Start-Sleep -Milliseconds 100
  }
  if ($null -eq $menu) { throw 'Right-click did not open the exit menu.' }
  $menuLocation = [FatFishFairySmoke.Native]::Location([int]($menu.ClientWidth / 2), [int]($menu.ClientHeight / 2))
  [void][FatFishFairySmoke.Native]::SendMessage($menu.Handle, 0x0200, [IntPtr]::Zero, $menuLocation)
  [void][FatFishFairySmoke.Native]::SendMessage($menu.Handle, 0x0201, [IntPtr]1, $menuLocation)
  [void][FatFishFairySmoke.Native]::PostMessage($menu.Handle, 0x0202, [IntPtr]::Zero, $menuLocation)
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
  Copy-Item -LiteralPath $executable -Destination (Join-Path $fixtureOutput 'FatFishFairy.exe')
  Copy-Item -LiteralPath (Join-Path $repository 'themes') -Destination (Join-Path $fixture 'themes') -Recurse
  $configPath = Join-Path $fixture 'env/config.json'
  [IO.File]::WriteAllText($configPath, '{"windowX":123,"windowY":91}', [Text.UTF8Encoding]::new($false))
  $application = Start-Process -FilePath (Join-Path $fixtureOutput 'FatFishFairy.exe') -WorkingDirectory $env:SystemRoot -WindowStyle Hidden -PassThru
  $main = Wait-MainWindow
  Assert-Window $main 123 91
  Start-Sleep -Milliseconds 200
  $frame = [FatFishFairySmoke.Native]::Capture($main.Handle, $ScreenshotPath)
  if ($frame.Colors -lt 64 -or $frame.OpaquePixels -lt 1000) { throw 'The fairy window did not render a character image.' }
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
  [FatFishFairySmoke.Native]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 300
  [void](Get-TestWindows)
  $moved = [FatFishFairySmoke.Native]::Describe($main.Handle)
  if ($moved.X -eq $main.X -and $moved.Y -eq $main.Y) { throw 'Dragging the visible character did not move the window.' }
  $saved = [IO.File]::ReadAllText($configPath) | ConvertFrom-Json
  Assert-Window $moved $saved.windowX $saved.windowY
  Close-ThroughMenu $moved
  Write-Output 'Left drag saved the new window position; right-click exit completed with code 0.'

  $application = Start-Process -FilePath (Join-Path $fixtureOutput 'FatFishFairy.exe') -WorkingDirectory $env:SystemRoot -WindowStyle Hidden -PassThru
  $restarted = Wait-MainWindow
  Assert-Window $restarted $saved.windowX $saved.windowY
  Close-ThroughMenu $restarted
  Write-Output 'Restart restored the dragged position from the isolated env folder; second menu exit completed with code 0.'
} catch {
  $testFailure = $_
  throw
} finally {
  [FatFishFairySmoke.Native]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
  [void][FatFishFairySmoke.Native]::SetCursorPos($cursor.X, $cursor.Y)
  if ($null -ne $application -and -not $application.HasExited) { $application.Kill(); [void]$application.WaitForExit(5000) }
  if ($null -ne $application) { $application.Dispose() }
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
