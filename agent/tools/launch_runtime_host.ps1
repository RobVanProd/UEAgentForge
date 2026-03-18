param(
    [string]$EngineRoot = "C:\Program Files\Epic Games\UE_5.7",
    [string]$ProjectPath = "C:\Users\Rob\Documents\Unreal Projects\UEAgentForge\agent\tmp\RuntimeHostProject\RuntimeHostProject.uproject",
    [string]$RemoteInfoUrl = "http://127.0.0.1:30010/remote/info",
    [int]$WaitSeconds = 180,
    [switch]$StopExisting
)

$ErrorActionPreference = "Stop"

function Disable-RecoveryState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }

    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $parent = Split-Path -Path $Path -Parent
    $leaf = Split-Path -Path $Path -Leaf
    $disabledLeaf = "$leaf.disabled_$timestamp"
    $disabledPath = Join-Path -Path $parent -ChildPath $disabledLeaf
    Rename-Item -LiteralPath $Path -NewName $disabledLeaf
    return $disabledPath
}

if ($StopExisting) {
    Get-Process UnrealEditor -ErrorAction SilentlyContinue | Stop-Process -Force
}

$resolvedProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$projectDir = Split-Path -Path $resolvedProjectPath -Parent
$projectAutosaves = Join-Path -Path $projectDir -ChildPath "Saved\Autosaves"
$globalRestoreFile = Join-Path -Path $env:LOCALAPPDATA -ChildPath "UnrealEngine\5.7\Saved\Autosaves\PackageRestoreData.json"

$disabledItems = @()
$disabledProjectAutosaves = Disable-RecoveryState -Path $projectAutosaves
if ($disabledProjectAutosaves) {
    $disabledItems += $disabledProjectAutosaves
}

$disabledGlobalRestore = Disable-RecoveryState -Path $globalRestoreFile
if ($disabledGlobalRestore) {
    $disabledItems += $disabledGlobalRestore
}

$editorExe = Join-Path -Path $EngineRoot -ChildPath "Engine\Binaries\Win64\UnrealEditor.exe"
if (-not (Test-Path -LiteralPath $editorExe)) {
    throw "UnrealEditor.exe not found at $editorExe"
}

$process = Start-Process -FilePath $editorExe -ArgumentList @(
    $resolvedProjectPath,
    "-NoSplash",
    "-log",
    '-ExecCmds="WebControl.StartServer"'
) -PassThru

$ready = $false
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline) {
    try {
        $response = Invoke-WebRequest -UseBasicParsing -Uri $RemoteInfoUrl -TimeoutSec 5
        if ($response.StatusCode -eq 200) {
            $ready = $true
            break
        }
    } catch {
        Start-Sleep -Seconds 2
    }
}

$result = [ordered]@{
    ok = $ready
    pid = $process.Id
    project_path = $resolvedProjectPath
    remote_info_url = $RemoteInfoUrl
    disabled_recovery_items = $disabledItems
}

if (-not $ready) {
    $result.error = "Remote Control server did not become ready within $WaitSeconds seconds."
}

$result | ConvertTo-Json -Depth 4

if (-not $ready) {
    exit 1
}
