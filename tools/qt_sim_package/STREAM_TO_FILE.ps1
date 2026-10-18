param(
    [Parameter(Mandatory = $true)][string]$ExecutablePath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$process = $null
$writer = $null
$exitCode = 1

try {
    if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
        throw "Simulator EXE does not exist: $ExecutablePath"
    }
    $directory = [System.IO.Path]::GetDirectoryName($OutputPath)
    [void][System.IO.Directory]::CreateDirectory($directory)

    # Qt 可在进程写入期间打开文件；每帧刷新，避免 UI 读到长时间缓存的旧数据。
    $stream = [System.IO.FileStream]::new($OutputPath,
        [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::ReadWrite)
    $writer = [System.IO.StreamWriter]::new($stream, [System.Text.UTF8Encoding]::new($false))
    $writer.AutoFlush = $true

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $ExecutablePath
    $startInfo.WorkingDirectory = [System.IO.Path]::GetDirectoryName($ExecutablePath)
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.CreateNoWindow = $true
    $startInfo.StandardOutputEncoding = [System.Text.Encoding]::ASCII
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw 'Simulator process could not start.' }

    # 这里只复制 C 输出的完整 TH3 行；不生成或重写温湿度和协议字段。
    while (($line = $process.StandardOutput.ReadLine()) -ne $null) {
        $writer.WriteLine($line)
        [Console]::WriteLine($line)
    }
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    if ($exitCode -ne 0) {
        [Console]::Error.WriteLine("Simulator exited with code $exitCode.")
    }
}
catch {
    [Console]::Error.WriteLine("[ERROR] $($_.Exception.Message)")
}
finally {
    if ($writer -ne $null) { $writer.Dispose() }
    if ($process -ne $null) {
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Dispose()
    }
}
exit $exitCode
