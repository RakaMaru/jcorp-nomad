#Requires -Version 5.1
<#
.SYNOPSIS
  Write filelist.txt: ASCII directory tree + flat paths, skipping local junk.

.DESCRIPTION
  Used by gh-filelist.bat. Skips .git, Python venvs, caches, and similar
  generated trees so a workbench .venv cannot bloat the committed listing.
#>
[CmdletBinding()]
param(
  [string] $Target = '',
  [string] $Out = 'filelist.txt'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $Target) { $Target = (Get-Location).Path }
$root = [IO.Path]::GetFullPath($Target)
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
  throw "Folder not found: $root"
}

$outPath = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }

$skipNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($n in @(
    '.git',
    '.venv',
    'venv',
    '__pycache__',
    '.idea',
    '.vscode',
    'mcps',
    'node_modules',
    '.pytest_cache',
    '.mypy_cache',
    '.ruff_cache'
  )) {
  [void]$skipNames.Add($n)
}

function Test-SkipName {
  param([string] $Name)
  return $skipNames.Contains($Name)
}

$treeLines = New-Object System.Collections.Generic.List[string]
$flat = New-Object System.Collections.Generic.List[string]
$dirCount = 0
$fileCount = 0

function Get-VisibleChildren {
  param([string] $Dir)
  $items = @(Get-ChildItem -LiteralPath $Dir -Force -ErrorAction SilentlyContinue)
  $kept = New-Object System.Collections.Generic.List[object]
  foreach ($item in $items) {
    if ($item.PSIsContainer -and (Test-SkipName $item.Name)) { continue }
    [void]$kept.Add($item)
  }
  return @($kept | Sort-Object { -not $_.PSIsContainer }, Name)
}

function Walk-Flat {
  param([string] $Dir)
  foreach ($item in @(Get-VisibleChildren -Dir $Dir)) {
    [void]$flat.Add($item.FullName)
    if ($item.PSIsContainer) {
      $script:dirCount++
      Walk-Flat -Dir $item.FullName
    } else {
      $script:fileCount++
    }
  }
}

# Directory-only tree (same idea as `tree /A`, no /F).
function Walk-DirTree {
  param(
    [string] $Dir,
    [string] $Prefix
  )
  $children = @(
    Get-ChildItem -LiteralPath $Dir -Force -Directory -ErrorAction SilentlyContinue |
      Where-Object { -not (Test-SkipName $_.Name) } |
      Sort-Object Name
  )
  $last = $children.Count - 1
  for ($i = 0; $i -le $last; $i++) {
    $item = $children[$i]
    $isLast = ($i -eq $last)
    if ($isLast) { $branch = '\---' } else { $branch = '+---' }
    if ($isLast) { $nextPrefix = $Prefix + '    ' } else { $nextPrefix = $Prefix + '|   ' }
    [void]$treeLines.Add($Prefix + $branch + $item.Name)
    Walk-DirTree -Dir $item.FullName -Prefix $nextPrefix
  }
}

Walk-DirTree -Dir $root -Prefix ''
Walk-Flat -Dir $root

$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$skipLabel = '.git, .venv, venv, __pycache__, .idea, .vscode, mcps, node_modules, caches'

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('============================================================')
[void]$sb.AppendLine("File list for: $root")
[void]$sb.AppendLine("Generated on : $stamp")
[void]$sb.AppendLine('Script       : gh-filelist.bat')
[void]$sb.AppendLine('============================================================')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('[TREE]')
[void]$sb.AppendLine("(excludes $skipLabel)")
[void]$sb.AppendLine($root)
foreach ($line in $treeLines) { [void]$sb.AppendLine($line) }
[void]$sb.AppendLine('')
[void]$sb.AppendLine('------------------------------------------------------------')
[void]$sb.AppendLine("[FLAT LIST  (excludes $skipLabel)]")
[void]$sb.AppendLine('------------------------------------------------------------')
foreach ($line in ($flat | Sort-Object)) { [void]$sb.AppendLine($line) }
[void]$sb.AppendLine('')
[void]$sb.AppendLine("[TOTALS  (excludes $skipLabel)]")
[void]$sb.AppendLine("Directories: $dirCount")
[void]$sb.AppendLine("Files      : $fileCount")

[IO.File]::WriteAllText($outPath, $sb.ToString(), [Text.UTF8Encoding]::new($false))

$bytes = (Get-Item -LiteralPath $outPath).Length
Write-Host ('[DONE] Wrote "{0}"  (Dirs: {1}  Files: {2}  Size: {3} bytes)' -f $outPath, $dirCount, $fileCount, $bytes)
