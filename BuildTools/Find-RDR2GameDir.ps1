# Locates the RDR2 install directory across all Steam libraries and
# prints it as the ONLY line of stdout on success (nothing else -- this
# is meant to be captured directly by a PostBuildEvent's `for /f`).
# Non-zero exit + stderr message on failure.
#
# Resolution order:
#   1. $env:RDR2_GAME_DIR, if set and valid (manual override)
#   2. Cached path from a previous successful run
#   3. Steam: registry install path(s) + all libraries listed in each
#      Steam install's steamapps\libraryfolders.vdf
#
# Vendored identically into every ScriptHookRDR2 ASI project in this
# repo family (PokerCheat, BlackjackCheat, DominoCheat, FFFCheat,
# FishingFix, YEEAHSM, ChallengeCheat) -- each project is its own separate git repo, so
# this can't live in one shared location outside any of them. If you fix
# a bug here, port the same fix to the other projects' copies too.

$ErrorActionPreference = 'Stop'

$cacheDir  = Join-Path $env:LOCALAPPDATA 'RDR2ASIDevTools'
$cacheFile = Join-Path $cacheDir 'gamedir.txt'

function Test-RDR2Dir([string]$dir) {
    return [bool]$dir -and (Test-Path (Join-Path $dir 'RDR2.exe'))
}

# 1. Manual override
if (Test-RDR2Dir $env:RDR2_GAME_DIR) {
    $env:RDR2_GAME_DIR
    exit 0
}

# 2. Cache (validated -- if the game moved/reinstalled, this is stale and
#    falls through to a fresh Steam scan below)
if (Test-Path $cacheFile) {
    $cached = (Get-Content -Path $cacheFile -Raw -ErrorAction SilentlyContinue)
    if ($cached) { $cached = $cached.Trim() }
    if (Test-RDR2Dir $cached) {
        $cached
        exit 0
    }
}

# 3. Steam: find every Steam install, then every library each one knows
#    about, then check each library for the game.
$steamRoots = New-Object System.Collections.Generic.List[string]
foreach ($entry in @(
    @{ Path = 'HKCU:\Software\Valve\Steam';                       Prop = 'SteamPath' },
    @{ Path = 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam';           Prop = 'InstallPath' },
    @{ Path = 'HKLM:\SOFTWARE\Valve\Steam';                       Prop = 'InstallPath' }
)) {
    $key = Get-ItemProperty -Path $entry.Path -ErrorAction SilentlyContinue
    if ($key -and $key.($entry.Prop)) {
        $steamRoots.Add(($key.($entry.Prop) -replace '/', '\'))
    }
}

$libraries = New-Object System.Collections.Generic.List[string]
foreach ($root in ($steamRoots | Select-Object -Unique)) {
    $libraries.Add($root)
    $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($m in (Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"')) {
            $libraries.Add(($m.Matches[0].Groups[1].Value -replace '\\\\', '\'))
        }
    }
}

foreach ($lib in ($libraries | Select-Object -Unique)) {
    $candidate = Join-Path $lib 'steamapps\common\Red Dead Redemption 2'
    if (Test-RDR2Dir $candidate) {
        New-Item -ItemType Directory -Path $cacheDir -Force -ErrorAction SilentlyContinue | Out-Null
        Set-Content -Path $cacheFile -Value $candidate -NoNewline
        $candidate
        exit 0
    }
}

Write-Error "Could not locate RDR2 install directory. Checked Steam libraries: $($libraries -join ', '). Set RDR2_GAME_DIR to override, or delete '$cacheFile' if it's stale."
exit 1
