param([string]$ReferenceRoot)
$ErrorActionPreference = 'Stop'
$distro = 'Ubuntu'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
if ([string]::IsNullOrWhiteSpace($ReferenceRoot)) {
    $candidate = Join-Path (Split-Path $repoRoot -Parent) 'CrossMix-OS-main'
    $ReferenceRoot = if (Test-Path -LiteralPath $candidate) { $candidate } else { $repoRoot }
}
if (-not (Test-Path -LiteralPath $ReferenceRoot -PathType Container)) {
    throw "Diretorio CrossMix de referencia nao encontrado: $ReferenceRoot"
}
$runScript = Join-Path $PSScriptRoot 'run.sh'
$linuxRun = (& wsl.exe --distribution $distro --exec wslpath -a $runScript).Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxRun) { throw 'WSL nao encontrou o script da previa.' }
$linuxReference = (& wsl.exe --distribution $distro --exec wslpath -a $ReferenceRoot).Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxReference) { throw 'WSL nao encontrou o diretorio de referencia.' }
& wsl.exe --distribution $distro --exec bash $linuxRun $linuxReference
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
