# PSScriptAnalyzer settings for the PowerShell under tools/. CI runs:
#   Invoke-ScriptAnalyzer -Path tools -Recurse -Settings tools/PSScriptAnalyzerSettings.psd1
# and fails on any Error or Warning. Rules left out, and why:
@{
    Severity     = @('Error', 'Warning')
    ExcludeRules = @(
        # The scripts are console tools: Write-Host is their output.
        'PSAvoidUsingWriteHost',
        # Script-level helpers, not cmdlets: no -WhatIf, and names such as Get-OpenChecklistItems.
        'PSUseShouldProcessForStateChangingFunctions',
        'PSUseSingularNouns',
        # Wrong for these scripts: parameters of a script are used inside its functions, and
        # env.ps1 sets variables for the scripts that dot-source it.
        'PSReviewUnusedParameter',
        'PSUseDeclaredVarsMoreThanAssignments',
        # PowerShell 7 reads UTF-8 without a BOM.
        'PSUseBOMForUnicodeEncodedFile'
    )
}
