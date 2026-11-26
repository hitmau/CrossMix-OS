param([string]$Distro = 'Ubuntu')
$ErrorActionPreference = 'Stop'
$scriptPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'run.sh')).Path
$linuxScript = (& wsl.exe --distribution $Distro --exec wslpath -a $scriptPath).Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxScript) {
    throw "WSL nao encontrou o script da previa. Distribuicao: $Distro"
}
& wsl.exe --distribution $Distro --exec bash $linuxScript
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
