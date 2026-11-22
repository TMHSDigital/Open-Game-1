# Stand-in for steamcmd.exe, used only by tools/steam/test-release.ps1. Never uploads anything.
#
# FAKE_STEAMCMD_MODE selects what a +run_app_build prints:
#   ok           success line with a BuildID, exit 0
#   error-exit0  an ERROR! line, exit 0 (steamcmd does this; the release must still fail)
#   no-buildid   looks successful, exit 0, but prints no BuildID
#   bad-branch   uploads (BuildID printed) but cannot set the build live: the branch does not exist
# FAKE_STEAMCMD_BUILDID   BuildID to print (default 98765)
# FAKE_STEAMCMD_BRANCHES  for +app_info_print: "public=100,development=200" (branch=buildid pairs)
# FAKE_STEAMCMD_INFO      for +app_info_print: 'fail' (error, exit 5) or 'nobranches' (no branches block)
# FAKE_STEAMCMD_LOG       if set, each invocation's arguments are appended to this file

$joined = $args -join ' '
if ($env:FAKE_STEAMCMD_LOG) { Add-Content -LiteralPath $env:FAKE_STEAMCMD_LOG -Value $joined }

Write-Output 'Redirecting stderr to fake log'
Write-Output 'Logging in user ... OK'

if ($args -contains '+app_info_print') {
    $appId = $args[[array]::IndexOf($args, '+app_info_print') + 1]
    if ($env:FAKE_STEAMCMD_INFO -eq 'fail') { Write-Output 'ERROR! Not logged on.'; exit 5 }
    if ($env:FAKE_STEAMCMD_INFO -eq 'nobranches') { Write-Output "AppID : $appId, change number : 0/0"; Write-Output 'No app info for this account.'; exit 0 }
    Write-Output "AppID : $appId, change number : 1/0"
    Write-Output "`"$appId`""
    Write-Output '{'
    Write-Output "`t`"depots`""
    Write-Output "`t{"
    Write-Output "`t`t`"branches`""
    Write-Output "`t`t{"
    foreach ($pair in ([string]$env:FAKE_STEAMCMD_BRANCHES -split ',' | Where-Object { $_ })) {
        $name, $build = $pair -split '=', 2
        Write-Output "`t`t`t`"$name`""
        Write-Output "`t`t`t{"
        Write-Output "`t`t`t`t`"buildid`"`t`t`"$build`""
        Write-Output "`t`t`t`t`"timeupdated`"`t`t`"1700000000`""
        Write-Output "`t`t`t}"
    }
    Write-Output "`t`t}"
    Write-Output "`t}"
    Write-Output '}'
    exit 0
}

if ($args -contains '+run_app_build') {
    $build = if ($env:FAKE_STEAMCMD_BUILDID) { $env:FAKE_STEAMCMD_BUILDID } else { '98765' }
    $mode = if ($env:FAKE_STEAMCMD_MODE) { $env:FAKE_STEAMCMD_MODE } else { 'ok' }
    Write-Output 'Building depot ...'
    switch ($mode) {
        'ok' {
            Write-Output "Successfully finished AppID build (BuildID $build)."
            exit 0
        }
        'error-exit0' {
            Write-Output 'ERROR! Failed to commit build for AppID : Failure'
            exit 0
        }
        'no-buildid' {
            Write-Output 'Uploading content ... done'
            exit 0
        }
        'bad-branch' {
            Write-Output "Successfully finished AppID build (BuildID $build)."
            Write-Output 'ERROR! Failed to set build live on branch development: Invalid Beta branch'
            exit 0
        }
        default {
            Write-Output "fake-steamcmd: unknown FAKE_STEAMCMD_MODE '$mode'"
            exit 2
        }
    }
}

exit 0
