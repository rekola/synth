# Signs files with a code signing certificate in a .pfx file, using signtool
# from the Windows SDK (on the GitHub runners).
# Usage: sign.ps1 -Pfx cert.pfx -Password secret [-TimestampUrl url] file...
# An empty -TimestampUrl signs without a timestamp, so the signature stops
# being valid when the certificate expires.
param(
  [Parameter(Mandatory = $true)][string]$Pfx,
  [Parameter(Mandatory = $true)][string]$Password,
  [string]$TimestampUrl = "http://timestamp.digicert.com",
  [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Files
)

$ErrorActionPreference = "Stop"

$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
  Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) { throw "signtool.exe was not found (Windows SDK)" }

$arguments = @("sign", "/f", $Pfx, "/p", $Password, "/fd", "SHA256")
if ($TimestampUrl) { $arguments += @("/tr", $TimestampUrl, "/td", "SHA256") }
& $signtool.FullName @arguments @Files
if ($LASTEXITCODE -ne 0) { throw "signtool failed with status $LASTEXITCODE" }
