param([Parameter(Mandatory = $true)][string]$Path)
$ErrorActionPreference = 'Stop'
# CMake may inherit PSModulePath from a different PowerShell edition.
Import-Module "$PSHOME/Modules/Microsoft.PowerShell.Security/Microsoft.PowerShell.Security.psd1"
Import-Module "$PSHOME/Modules/Microsoft.PowerShell.Management/Microsoft.PowerShell.Management.psd1"
$signature = Get-AuthenticodeSignature -LiteralPath $Path
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation(?:,|$)') {
    throw 'The redistributable must have a valid Microsoft signature.'
}
$info = (Get-Item -LiteralPath $Path).VersionInfo
if ($info.ProductName -notmatch 'Redistributable.*x64|x64.*Redistributable') {
    throw 'Expected the Microsoft Visual C++ x64 Redistributable.'
}
Write-Output ('{0}.{1}.{2}.{3}' -f $info.ProductMajorPart, $info.ProductMinorPart, $info.ProductBuildPart, $info.ProductPrivatePart)
