# Launches the Unreal Editor for CombatSimulator in the background and waits until the
# Unreal MCP server (Epic's in-editor Model Context Protocol endpoint) answers.
#
# Requires the ModelContextProtocol plugin to be enabled (it is, in Game/CombatSimulator.uproject).
# The project ships bAutoStartServer=False (Game/Config/DefaultEditorPerProjectUserSettings.ini), so
# this script passes -ModelContextProtocolStartServer to start the server for this launch. To have it
# start every time you open the editor yourself, turn on Editor Preferences, Plugins, Model Context
# Protocol, Auto Start Server (saved per user under Game/Saved/, never committed).
#
# Usage: pwsh tools/ue/editor.ps1 [-TimeoutSec 600]

param(
    [int]$TimeoutSec = 600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-McpEndpoint {
    try {
        # Any HTTP answer (even 400 or 405) means the server is listening. Connection refused throws.
        $null = Invoke-WebRequest -Uri $McpUrl -Method Get -TimeoutSec 5 -SkipHttpErrorCheck -UseBasicParsing
        return $true
    }
    catch {
        return $false
    }
}

try {
    . "$PSScriptRoot\env.ps1"
    Assert-Engine
    Assert-Project

    if ((Get-ProjectEditorProcess).Count -gt 0) {
        Write-Host "UnrealEditor already has $ProjectName open."
    }
    else {
        Write-Host "Starting $Editor"
        Start-Process -FilePath $Editor -ArgumentList "`"$Proj`"", '-ModelContextProtocolStartServer' | Out-Null
    }

    Write-Host "Waiting up to $TimeoutSec s for $McpUrl ..."
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-McpEndpoint) {
            Write-Host 'Unreal MCP endpoint is up.'
            exit 0
        }
        if ((Get-ProjectEditorProcess).Count -eq 0) {
            throw "UnrealEditor exited before the MCP endpoint came up. Check $LogsDir\$ProjectName.log."
        }
        Start-Sleep -Seconds 5
    }
    throw "MCP endpoint did not answer within $TimeoutSec s. Is the ModelContextProtocol plugin enabled in Game/$ProjectName.uproject?"
}
catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
