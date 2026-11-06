# Re-creates vendor/node_modules from the pinned packages below. The extension's runtime dependencies are checked in,
# so run this only when changing a pin, and review the resulting diff before committing.
#
# Usage: pwsh scripts/vendor.ps1

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'NpmPackage.ps1')

# vscode-languageclient and its dependencies, from upstream's package-lock.json.
$packages = @(
	@{ Name = 'vscode-languageclient'; Version = '9.0.1'; Integrity = 'sha512-JZiimVdvimEuHh5olxhxkht09m3JzUGwggb5eRUkzzJhZ2KjCN0nh55VfiED9oez9DyF8/fz1g1iBV3h+0Z2EA==' }
	@{ Name = 'vscode-languageserver-protocol'; Version = '3.17.5'; Integrity = 'sha512-mb1bvRJN8SVznADSGWM9u/b07H7Ecg0I3OgXDuLdn307rl/J3A9YD6/eYOssqhecL27hK1IPZAsaqh00i/Jljg==' }
	@{ Name = 'vscode-jsonrpc'; Version = '8.2.0'; Integrity = 'sha512-C+r0eKJUIfiDIfwJhria30+TYWPtuHJXHtI7J0YlOmKAo7ogxP20T0zxB7HZQIFhIyvoBPwWskjxrvAtfjyZfA==' }
	@{ Name = 'vscode-languageserver-types'; Version = '3.17.5'; Integrity = 'sha512-Ld1VelNuX9pdF39h2Hgaeb5hEZM2Z3jUrrMgWQAu82jMtZp7p3vJT3BzToKtZI7NgQssZje5o0zryOrhQvzQAg==' }
	@{ Name = 'semver'; Version = '7.6.2'; Integrity = 'sha512-FNAIBWCx9qcRhoHcgcJ0gvU7SN1lYU2ZXuSfl04bSC5OpvDHFyJCjdNHomPXxjQlCBU67YW64PzY7/VIEH7F2w==' }
	@{ Name = 'minimatch'; Version = '5.1.9'; Integrity = 'sha512-7o1wEA2RyMP7Iu7GNba9vc0RWWGACJOCZBJX2GJWip0ikV+wcOsgVuY9uE8CPiyQhkGFSlhuSkZPavN7u1c2Fw==' }
	@{ Name = 'brace-expansion'; Version = '2.1.7'; Integrity = 'sha512-uZbew1NqdmPDTMJ8ah1y+b+9QEJrfkXFk3RcTQw3X0jW/xRUvFKsg1CfQdSYGdTbXZWExtU3J3ccxtnfw1Fi0g==' }
	@{ Name = 'balanced-match'; Version = '1.0.2'; Integrity = 'sha512-3oSeUO0TMV67hN1AmbXsK4yaqU7tjiHlbxRDZOpH0KW9+CeX4bRAaX0Anxt0tx2MrpRpWwQaPwIlISEJhYU5Pw==' }
)

$vendor = Join-Path (Split-Path $PSScriptRoot -Parent) 'vendor/node_modules'
if (Test-Path $vendor) { Remove-Item -Recurse -Force $vendor }

$notices = [Text.StringBuilder]::new()
[void]$notices.Append("# Third-party notices`n`nThe bundled extension includes the following packages.`n")

foreach ($package in $packages)
{
	$destination = Join-Path $vendor $package.Name
	Expand-NpmPackage $package.Name $package.Version $package.Integrity $destination
	Write-Host "$($package.Name)@$($package.Version) ok"

	$license = Get-ChildItem $destination -File | Where-Object Name -match '^licen[cs]e' | Select-Object -First 1
	if (-not $license) { throw "$($package.Name) has no licence file" }
	[void]$notices.Append("`n## $($package.Name) $($package.Version)`n`n``````text`n$((Get-Content $license.FullName -Raw).Trim())`n```````n")
}

Set-Content (Join-Path (Split-Path $vendor -Parent) 'NOTICES.md') $notices.ToString() -Encoding utf8NoBOM -NoNewline
