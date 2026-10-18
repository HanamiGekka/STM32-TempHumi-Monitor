param(
    [Parameter(Mandatory=$true)][string]$Destination,
    [Parameter(Mandatory=$true)][string]$ExpectedVersion,
    [string]$Sources='', [int]$Profile=-1, [int]$Seconds=18,
    [string]$WriteImage='', [string]$ProbeSN='', [string]$ExpectedUID='',
    [string]$ExpectedIdentitySha256='', [ValidatePattern('^COM[0-9]+$')][string]$SelectedPort='COM13'
)
$ErrorActionPreference='Stop'
if($ExpectedVersion -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Invalid expected version' }
if($Sources -and $Sources -notmatch '^(REAL|SIM),(REAL|SIM)$') { throw 'Invalid explicit sources' }
if($Profile -lt -1 -or $Profile -gt 3) { throw 'Invalid profile' }
$cli='E:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
$port=[IO.Ports.SerialPort]::new($SelectedPort,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.ReadTimeout=100;$port.WriteTimeout=500;$port.ReadBufferSize=65536
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.Handshake=[IO.Ports.Handshake]::None
$memory=[IO.MemoryStream]::new();$buffer=[byte[]]::new(8192);$phase=0;$generation='';$lastQuery=0
try {
    $port.Open()
    if($WriteImage) {
        if(!$ProbeSN -or !$ExpectedUID) { throw 'Explicit probe and UID required' }
        $uidPath=$Destination+'.uid_immediate.bin'
        & $cli -c port=SWD "sn=$ProbeSN" freq=1000 -u '0x1FFFF7E8' 12 $uidPath *> ($Destination+'.uid_immediate.log')
        if($LASTEXITCODE -ne 0 -or [BitConverter]::ToString([IO.File]::ReadAllBytes($uidPath)).Replace('-','').ToLowerInvariant() -ne $ExpectedUID) { throw 'Immediate target identity mismatch' }
        $sentinel=$Destination+'.write_started.json'
        $once=[IO.File]::Open($sentinel,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $text=[Text.Encoding]::UTF8.GetBytes('{"single_program_operation_started":true}');$once.Write($text,0,$text.Length) } finally { $once.Dispose() }
        & $cli -c port=SWD "sn=$ProbeSN" freq=1000 -w $WriteImage '0x08000000' -v -rst *> ($Destination+'.program.log')
        if($LASTEXITCODE -ne 0) { throw 'Single programming operation failed; no retry' }
    }
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt $Seconds) {
        if($port.BytesToRead -gt 0) {
            $n=$port.Read($buffer,0,[Math]::Min($buffer.Length,$port.BytesToRead));$memory.Write($buffer,0,$n)
        }
        $all=[Text.Encoding]::ASCII.GetString($memory.ToArray());$text=$all
        if($WriteImage) {
            $boot=$text.LastIndexOf('BOOT OK '+$ExpectedVersion)
            if($boot -lt 0) { Start-Sleep -Milliseconds 20;continue }
            $text=$text.Substring($boot)
        }
        $frames=@([regex]::Matches($text,'(?m)^TH3,[^\r\n]+\r?$') | Where-Object { $_.Value.Trim().Split(',').Count -eq 23 })
        if($phase -eq 0 -and $frames.Count -gt 0) { $port.Write("CMD,PING`r`n");$phase=1 }
        elseif($phase -eq 1 -and $text.Contains('ACK,PING')) { $port.Write("CMD,STATUS?`r`n");$phase=2 }
        elseif($phase -eq 2 -and $text.Contains('RSP,STATUS,1,'+$ExpectedVersion+',')) { $port.Write("CMD,ID?`r`n");$phase=11 }
        elseif($phase -eq 11) {
            # ES096 2.2.2: DBGMCU code 0 is unavailable in user mode, not a blank UID.
            $identity=[regex]::Match($text,'(?m)^RSP,ID,1,STM32F1,([0-9a-fA-F]{24}),(64|128),(0|1040)\r?$')
            if($identity.Success) {
                $hex=$identity.Groups[1].Value.ToLowerInvariant();$uidBytes=[byte[]]::new(12)
                if($hex -match '^(0{24}|f{24})$' -or ($hex.Substring(0,8) -eq $hex.Substring(8,8) -and $hex.Substring(0,8) -eq $hex.Substring(16,8))) { throw 'Blank, erased or repeated factory UID rejected' }
                for($i=0;$i -lt 12;$i++) { $uidBytes[$i]=[Convert]::ToByte($hex.Substring(2*$i,2),16) }
                $algorithm=[Security.Cryptography.SHA256]::Create()
                try { $digest=[BitConverter]::ToString($algorithm.ComputeHash($uidBytes)).Replace('-','').ToLowerInvariant() } finally { $algorithm.Dispose() }
                if($ExpectedIdentitySha256 -and $digest -ne $ExpectedIdentitySha256) { throw 'UART MCU UID differs from verified target' }
                $port.Write("CMD,MODE?`r`n");$phase=3
            }
        }
        elseif($phase -eq 3 -and $text.Contains('RSP,MODE,1,')) { $port.Write("CMD,CAL?`r`n");$phase=4 }
        elseif($phase -eq 4 -and $text.Contains('RSP,CAL,1,')) {
            if($Sources) { $port.Write("CMD,SET,BOTH,$Sources`r`n");$phase=5 }
            elseif($Profile -ge 0) { $port.Write("CMD,CAL,$Profile`r`n");$phase=7 }
            else { $phase=10 }
        }
        elseif($phase -eq 5) {
            $ack=[regex]::Match($text,'ACK,SET,([0-9]+),'+$Sources)
            if($ack.Success) { $generation=$ack.Groups[1].Value;$port.Write("CMD,MODE?`r`n");$lastQuery=$timer.ElapsedMilliseconds;$phase=6 }
        }
        elseif($phase -eq 6) {
            $mode='RSP,MODE,1,'+$generation+','+$Sources+','+$generation+','+$Sources
            $effective=$false
            foreach($frame in $frames) { $fields=$frame.Value.Trim().Split(',');if(($fields[18]+','+$fields[19]) -eq $Sources -and $fields[22] -eq $generation) { $effective=$true } }
            if($text.Contains($mode) -and $effective) {
                if($Profile -ge 0) { $port.Write("CMD,CAL,$Profile`r`n");$phase=7 } else { $phase=10 }
            } elseif($timer.ElapsedMilliseconds-$lastQuery -gt 1200) { $port.Write("CMD,MODE?`r`n");$lastQuery=$timer.ElapsedMilliseconds }
        }
        elseif($phase -eq 7 -and $text.Contains("ACK,CAL,$Profile")) { $lastQuery=$timer.ElapsedMilliseconds;$phase=8 }
        elseif($phase -eq 8 -and $timer.ElapsedMilliseconds-$lastQuery -gt 1200) { $port.Write("CMD,CAL?`r`n");$lastQuery=$timer.ElapsedMilliseconds;$phase=9 }
        elseif($phase -eq 9) {
            if($text.Contains("RSP,CAL,1,$Profile,$Profile")) { $phase=10 }
            elseif($timer.ElapsedMilliseconds-$lastQuery -gt 1200) { $port.Write("CMD,CAL?`r`n");$lastQuery=$timer.ElapsedMilliseconds }
        }
        Start-Sleep -Milliseconds 20
    }
} finally {
    if($port.IsOpen) { $port.Close() }
    [IO.File]::WriteAllBytes($Destination,$memory.ToArray());$port.Dispose();$memory.Dispose()
}
if($phase -ne 10) { throw "Initialized control not fully confirmed: phase=$phase; original bytes preserved" }
Write-Output "PASS expected=$ExpectedVersion phase=$phase COM_released=True"
