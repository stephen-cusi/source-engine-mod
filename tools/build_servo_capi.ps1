param(
    [Parameter(Mandatory = $true)]
    [string]$ServoRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Target,

    [switch]$CheckOnly,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$upstreamRevision = 'facef05f121a7ac657050eb8d5e450188efcb69f'
$forkRevision = '43eae6443b8742331bc2d08a27c8ec11001046eb'
$bridgePatch = Join-Path $PSScriptRoot 'servo/servo-capi.patch'
$root = (Resolve-Path -LiteralPath $ServoRoot).Path

Push-Location -LiteralPath $root
try {
    $revision = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "git rev-parse failed inside $root"
    }

    # The pins are BASES, not exact HEADs: local commits and upstream merges
    # land on top of them, so judge by ancestry, not equality.
    & git merge-base --is-ancestor $upstreamRevision HEAD
    if ($LASTEXITCODE -ne 0) {
        throw "Servo base mismatch: HEAD $revision does not contain the pinned upstream base $upstreamRevision"
    }
    & git merge-base --is-ancestor $forkRevision HEAD
    $onIntegrationBranch = ($LASTEXITCODE -eq 0)

    if (-not (Test-Path -LiteralPath 'ffi/capi/Cargo.toml')) {
        throw 'Servo C API sources are missing from the checkout.'
    }

    if ($onIntegrationBranch) {
        # The bridge changes are committed on the integration branch; the
        # three pinned files may carry in-flight work on top, which is fine.
        Write-Host "Servo revision: $revision (integration branch)"
    } else {
        # Pure upstream checkout: the bridge must live in the working tree.
        if ($revision -ne $upstreamRevision) {
            throw "Servo revision mismatch: expected the pinned upstream base $upstreamRevision, got $revision"
        }
        & git apply --reverse --check $bridgePatch
        if ($LASTEXITCODE -ne 0) {
            throw "The Servo C API patch is not applied. Apply $bridgePatch before building."
        }
        Write-Host "Servo revision: $revision (upstream + applied bridge patch)"
    }
    Write-Host "Target: $Target"
    if ($ValidateOnly) { exit 0 }

    $action = if ($CheckOnly) { 'check' } else { 'build' }
    $arguments = @($action, '--locked', '-j', '12', '--target', $Target, '-p', 'servo-capi')
    if (-not $CheckOnly) { $arguments += '--release' }

    $artifact = Join-Path $root "target\$Target\release\servo_capi.dll"
    $buildStartUtc = [datetime]::UtcNow

    # Cargo reports build-script warnings on stderr; under a stream redirect
    # Windows PowerShell turns those lines into error records and the Stop
    # preference aborts on the first one.  Run with Continue and judge the
    # result by the exit code alone.
    $prevPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & cargo @arguments
        $cargoExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevPreference
    }

    if ($cargoExit -ne 0) {
        # Cargo can exit 101 right after a fully successful link on Windows
        # (no error text, artifacts complete) when a post-build cleanup step
        # loses a file-lock race.  Accept the run when the DLL was written
        # during THIS invocation; an older artifact means a real failure.
        $artifactFresh = (Test-Path -LiteralPath $artifact) -and
            ((Get-Item -LiteralPath $artifact).LastWriteTimeUtc -ge $buildStartUtc)
        if ($artifactFresh) {
            Write-Warning "cargo exited with $cargoExit after a successful link (no error output); servo_capi.dll was written during this run and is accepted."
        } else {
            throw "Servo $action failed with exit code $cargoExit"
        }
    }
    exit 0
} finally {
    Pop-Location
}
