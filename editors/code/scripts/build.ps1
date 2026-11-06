# Bundles the extension with a pinned esbuild binary, copies in the server, and optionally packs a .vsix, without npm.
#
# Usage: pwsh scripts/build.ps1 -Server <path to built luau-lsp executable> [-Package] [-Development]
# Install the result with `code --install-extension <vsix>`.

param(
	[Parameter(Mandatory)][string]$Server,
	[switch]$Package,
	[switch]$Development
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'NpmPackage.ps1')

$root = Split-Path $PSScriptRoot -Parent
$manifest = Get-Content (Join-Path $root 'package.json') -Raw | ConvertFrom-Json

$esbuildVersion = '0.28.1'
$esbuildIntegrity = @{
	'win32-x64' = 'sha512-bm4Mowrv+GXMlpWX++EcXw/iLyd1o3+bJkC2DkWXYVvgZCqD/bSj9ctZeAMC3cIxgjRVR2Dufaiu4YPxr5gW1A=='
	'linux-x64' = 'sha512-u/anNYF2mmVOEDwLtnQ1wOr3EZ9sTNGLWrsYGYwHWzGA3Si84IOkHXlbWTD1NB+9/1lcnweYKO54uhxZydNzfA=='
	'linux-arm64' = 'sha512-yHs+0uc8+nvEAfAfxrWQKK5peSNzBc4PegcMO0EJ2hT71uA7vB8Ihg2e77R2P7SG5uYjPbHlLLmve4LLLRCf0g=='
	'darwin-x64' = 'sha512-zfdzgK9ACBNZLI/CyHTOx81SyNbM6YXn7rxSgX97VjyiPl9W1i4Ka4fgKECEoFCKGpvBj5qArWIGgQjOwkgskQ=='
	'darwin-arm64' = 'sha512-TZbWkQY7kvTAXbXUT7uVACR5cMHsDiSz9z7ZKAX/RTq/WJEk3QyRr0wZpNhBDX+/0CtdqUIJlOiodQcta6tY3Q=='
}

$os = if ($IsWindows) { 'win32' } elseif ($IsMacOS) { 'darwin' } else { 'linux' }
$arch = if ([Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64' } else { 'x64' }
$platform = "$os-$arch"
if (-not $esbuildIntegrity.ContainsKey($platform)) { throw "No pinned esbuild for $platform" }

$esbuildDir = Join-Path $root ".cache/esbuild-$esbuildVersion-$platform"
$esbuild = if ($IsWindows) { Join-Path $esbuildDir 'esbuild.exe' } else { Join-Path $esbuildDir 'bin/esbuild' }
if (-not (Test-Path $esbuild))
{
	Expand-NpmPackage "@esbuild/$platform" $esbuildVersion $esbuildIntegrity[$platform] $esbuildDir
}

if (-not (Test-Path $Server)) { throw "Server binary not found: $Server" }

$dist = Join-Path $root 'dist'
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }

$esbuildArgs = @(
	(Join-Path $root 'src/extension.ts'), '--bundle', '--platform=node', '--format=cjs', '--target=node18',
	'--external:vscode', '--log-level=warning', "--outfile=$(Join-Path $dist 'extension.js')"
)
$esbuildArgs += if ($Development) { '--sourcemap' } else { '--minify' }

$env:NODE_PATH = Join-Path $root 'vendor/node_modules'
try { & $esbuild @esbuildArgs }
finally { Remove-Item Env:NODE_PATH }
if ($LASTEXITCODE -ne 0) { throw 'esbuild failed' }

$bin = Join-Path $root 'bin'
New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item $Server (Join-Path $bin $(if ($IsWindows) { 'server.exe' } else { 'server' })) -Force

if (-not $Package) { return }

$stage = Join-Path $root ".cache/vsix"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$extension = Join-Path $stage 'extension'
New-Item -ItemType Directory -Force $extension | Out-Null

foreach ($item in 'package.json', 'language-configuration.json', 'dist', 'bin', 'syntaxes', 'schemas', 'assets')
{
	Copy-Item (Join-Path $root $item) $extension -Recurse
}
Copy-Item (Join-Path $root '../../LICENSE.md') (Join-Path $extension 'LICENSE.md')
Copy-Item (Join-Path $root 'vendor/NOTICES.md') (Join-Path $extension 'NOTICES.md')

$escape = { param($text) [Security.SecurityElement]::Escape($text) }
@"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="$($manifest.name)" Version="$($manifest.version)" Publisher="$($manifest.publisher)" />
    <DisplayName>$(& $escape $manifest.displayName)</DisplayName>
    <Description xml:space="preserve">$(& $escape $manifest.description)</Description>
    <Tags>$(& $escape ($manifest.keywords -join ','))</Tags>
    <Categories>$(& $escape ($manifest.categories -join ','))</Categories>
    <GalleryFlags>Public</GalleryFlags>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$($manifest.engines.vscode)" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
    </Properties>
    <License>extension/LICENSE.md</License>
    <Icon>extension/$($manifest.icon)</Icon>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.License" Path="extension/LICENSE.md" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Icons.Default" Path="extension/$($manifest.icon)" Addressable="true" />
  </Assets>
</PackageManifest>
"@ | Set-Content (Join-Path $stage 'extension.vsixmanifest') -Encoding utf8NoBOM

$extensions = Get-ChildItem $stage -Recurse -File | ForEach-Object { $_.Extension.ToLowerInvariant() } | Where-Object { $_ } | Sort-Object -Unique
$types = foreach ($ext in $extensions)
{
	$type = switch ($ext)
	{
		'.json' { 'application/json' }
		'.js' { 'application/javascript' }
		'.md' { 'text/markdown' }
		'.png' { 'image/png' }
		'.svg' { 'image/svg+xml' }
		'.vsixmanifest' { 'text/xml' }
		default { 'application/octet-stream' }
	}
	"  <Default Extension=`"$ext`" ContentType=`"$type`" />"
}
@"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
$($types -join "`n")
</Types>
"@ | Set-Content -LiteralPath (Join-Path $stage '[Content_Types].xml') -Encoding utf8NoBOM

$vsix = Join-Path $root "$($manifest.name)-$($manifest.version)-$platform.vsix"
if (Test-Path $vsix) { Remove-Item $vsix }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $vsix)
Write-Host "Packed $vsix"
