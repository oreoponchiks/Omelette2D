param([string]$Executable = 'build/Debug/Omelette2D.exe', [switch]$ObjectDemo)
$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class SmokeWindow {
    public delegate bool EnumProc(IntPtr window, IntPtr param);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr param);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder name, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder name, int count);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    public static IntPtr[] Windows(uint process) {
        var result = new List<IntPtr>();
        EnumWindows((window, param) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == process) result.Add(window);
            return true;
        }, IntPtr.Zero);
        return result.ToArray();
    }
    public static string Class(IntPtr window) {
        var name = new StringBuilder(256);
        GetClassName(window, name, name.Capacity);
        return name.ToString();
    }
    public static string Title(IntPtr window) {
        var name = new StringBuilder(512);
        GetWindowText(window, name, name.Capacity);
        return name.ToString();
    }
}
'@

$target = (Resolve-Path -LiteralPath $Executable).Path
$projectRoot = Split-Path -Parent $PSScriptRoot
$runDirectory = Join-Path $projectRoot ('build/runtime-smoke/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
# Keep test resize operations from changing the user's saved ImGui layout.
$layout = Join-Path $projectRoot 'imgui.ini'
if (Test-Path -LiteralPath $layout) { Copy-Item -LiteralPath $layout -Destination $runDirectory }
$start = @{ FilePath=$target; WorkingDirectory=$runDirectory; PassThru=$true; WindowStyle='Hidden' }
if ($ObjectDemo) { $start.ArgumentList = '--object-demo' }
$process = Start-Process @start
try {
    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        if ($process.HasExited) { throw "App exited during startup: $($process.ExitCode)" }
        foreach ($candidate in [SmokeWindow]::Windows($process.Id)) {
            if ([SmokeWindow]::Class($candidate) -eq 'Omelette2DWindow') { $window = $candidate }
            if ([SmokeWindow]::Class($candidate) -eq '#32770') {
                throw "Unexpected dialog: $([SmokeWindow]::Title($candidate))"
            }
        }
        if ($window -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Engine window was not created.' }
    Start-Sleep -Seconds 2
    foreach ($size in @(@(800, 600), @(1600, 900), @(640, 480), @(1280, 720))) {
        # SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE: keep the test window hidden.
        if (![SmokeWindow]::SetWindowPos($window, [IntPtr]::Zero, 0, 0, $size[0], $size[1], 0x16)) {
            throw 'Window resize failed.'
        }
        Start-Sleep -Milliseconds 500
    }
    # Exercise the minimized-size path without displaying or focusing the window.
    [SmokeWindow]::SendMessage($window, 0x0005, [IntPtr]1, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 250
    [SmokeWindow]::SendMessage($window, 0x0005, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Seconds 1
    foreach ($candidate in [SmokeWindow]::Windows($process.Id)) {
        if ([SmokeWindow]::Class($candidate) -eq '#32770') {
            throw "Unexpected dialog: $([SmokeWindow]::Title($candidate))"
        }
    }
    if ($process.HasExited) { throw "App exited during rendering: $($process.ExitCode)" }
    [SmokeWindow]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    if (!$process.WaitForExit(10000)) { throw 'App did not exit cleanly.' }
    if ($process.ExitCode -ne 0) { throw "App exited with code $($process.ExitCode)" }
    Write-Output 'Runtime smoke test passed: startup, four resizes, size notifications, and clean exit.'
} finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id }
    $process.Dispose()
}
