$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')
$before = (Get-FileHash -LiteralPath 'bin\dinput8.dll').Hash
$previous = $env:CL
try {
    # Inject a compiler error without editing any project source.
    $env:CL = '/FI"__MK9_INTENTIONAL_MISSING_HEADER_94F26A__.hpp"'
    & cmd /d /c 'build_x86.bat > tests\build_failure.log 2>&1'
    $result = $LASTEXITCODE
} finally { $env:CL = $previous }
if ($result -eq 0) { throw 'A failed compilation incorrectly returned success' }
if ((Get-FileHash -LiteralPath 'bin\dinput8.dll').Hash -ne $before) { throw 'Failed build modified the last good DLL' }
if (Select-String -LiteralPath 'tests\build_failure.log' -SimpleMatch '[+] Build succeeded:') { throw 'Failed build printed a success marker' }
if (!(Select-String -LiteralPath 'tests\build_failure.log' -SimpleMatch '__MK9_INTENTIONAL_MISSING_HEADER_94F26A__.hpp')) { throw 'Failure did not exercise the intended compiler error' }
'PASS: compiler failure returns nonzero, preserves the last good DLL, and does not report success.'
