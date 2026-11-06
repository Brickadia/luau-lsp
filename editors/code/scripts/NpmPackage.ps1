# Fetches one npm package tarball, checks it against its pinned sha512 and unpacks it, without npm or install scripts.
function Expand-NpmPackage([string]$Name, [string]$Version, [string]$Integrity, [string]$Destination)
{
	$baseName = ($Name -split '/')[-1]
	$url = "https://registry.npmjs.org/$Name/-/$baseName-$Version.tgz"

	$work = Join-Path ([IO.Path]::GetTempPath()) ("npm-" + [guid]::NewGuid())
	New-Item -ItemType Directory $work | Out-Null

	try
	{
		$tarball = Join-Path $work 'package.tgz'
		Invoke-WebRequest -Uri $url -OutFile $tarball -UseBasicParsing

		$stream = [IO.File]::OpenRead($tarball)
		try { $actual = 'sha512-' + [Convert]::ToBase64String([Security.Cryptography.SHA512]::Create().ComputeHash($stream)) }
		finally { $stream.Dispose() }

		if ($actual -ne $Integrity)
		{
			throw "$Name@$Version failed its integrity check: expected $Integrity, got $actual"
		}

		tar -xzf $tarball -C $work
		if ($LASTEXITCODE -ne 0) { throw "Failed to unpack $Name@$Version" }

		if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
		New-Item -ItemType Directory -Force (Split-Path $Destination -Parent) | Out-Null
		Move-Item (Join-Path $work 'package') $Destination
	}
	finally
	{
		Remove-Item -Recurse -Force $work
	}
}
