# Source-only port: this script has not yet been executed on Windows.
# Compile by default; writing hardware requires explicit -Flash and -Port.
[CmdletBinding()]
param(
    [switch]$WithUploader,
    [switch]$Flash,
    [string]$Port,
    [string]$ArduinoCli = 'arduino-cli',
    [string]$Python = 'py'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Flash -and [string]::IsNullOrWhiteSpace($Port)) {
    throw 'Flashing requires -Port COMx (for example -Port COM5).'
}
function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}
$Root = Split-Path -Parent $PSScriptRoot
$Fqbn = 'esp32:esp32:esp32:PartitionScheme=min_spiffs,UploadSpeed=115200'
$BoardFlag = '-DCYD_BOARD=CYD_BOARD_LCDWIKI_E32R28T'
$Player = Join-Path $Root 'app/build/e32r28t/player'
$Uploader = Join-Path $Root 'app/build/e32r28t/uploader'
Push-Location $Root
try {
    # Same sync as tools/sync_shared.sh, without requiring Git Bash.
    $Targets = @('app') + @(Get-ChildItem (Join-Path $Root 'stage0') -Directory |
        ForEach-Object { 'stage0/' + $_.Name })
    foreach ($Target in $Targets) {
        Copy-Item 'config/board.h' (Join-Path $Target 'board.h') -Force
    }
    # Separate build paths prevent player/uploader object contamination.
    # Both C and C++ flags apply the identical board selection to all sources.
    $Common = @('compile', '--fqbn', $Fqbn, '--warnings', 'none', '--jobs', '4')
    Invoke-Checked $ArduinoCli ($Common + @(
        '--build-property', "compiler.cpp.extra_flags=$BoardFlag",
        '--build-property', "compiler.c.extra_flags=$BoardFlag",
        '--build-path', "$Player/cache", '--output-dir', $Player, 'app'))
    if ($WithUploader) {
        Invoke-Checked $ArduinoCli ($Common + @(
            '--build-property', "compiler.cpp.extra_flags=$BoardFlag -DCYD_UPLOADER",
            '--build-property', "compiler.c.extra_flags=$BoardFlag -DCYD_UPLOADER",
            '--build-path', "$Uploader/cache", '--output-dir', $Uploader, 'app'))
    }
    # Record the exact local source commit alongside the binaries.
    $Revision = & git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record source commit.' }
    $Dirty = & git status --porcelain
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record source status.' }
    @("source=$Revision", "dirty=$([bool]$Dirty)", "board=LCDWIKI_E32R28T",
      "fqbn=$Fqbn", "uploader=$WithUploader") |
        Set-Content (Join-Path $Player 'source.txt')
    if ($WithUploader) { Copy-Item (Join-Path $Player 'source.txt') $Uploader -Force }
    if ($Flash) {
        # CLI uploads player + bootloader + min_spiffs table + OTA selector.
        # Does not erase NVS; no merged factory-reset image is written.
        Invoke-Checked $ArduinoCli @('upload', '--fqbn', $Fqbn, '--port', $Port,
            '--input-dir', $Player, 'app')
        if ($WithUploader) {
            # esptool v5 syntax. Never write the uploader at app0 / 0x10000.
            Invoke-Checked $Python @('-m', 'esptool', '--chip', 'esp32', '--port', $Port,
                '--baud', '115200', 'write-flash', '0x1F0000',
                (Join-Path $Uploader 'app.ino.bin'))
        }
    }
}
finally { Pop-Location }
