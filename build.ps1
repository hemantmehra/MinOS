$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$buildDirectory = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null

$nasm = Get-Command nasm.exe -ErrorAction SilentlyContinue
if (-not $nasm)
{
    $nasmCandidates = @(
        (Join-Path $env:LOCALAPPDATA 'bin\NASM\nasm.exe'),
        (Join-Path $env:ProgramFiles 'NASM\nasm.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'NASM\nasm.exe'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links\nasm.exe')
    )
    foreach ($candidate in $nasmCandidates)
    {
        if (Test-Path $candidate)
        {
            $nasm = Get-Item $candidate
            break
        }
    }
}
if (-not $nasm)
{
    throw 'NASM was not found. Install NASM or add nasm.exe to PATH.'
}
$nasmPath = $nasm.Source
if (-not $nasmPath)
{
    $nasmPath = $nasm.FullName
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere))
{
    throw "Visual Studio Installer was not found at $vswhere."
}

$visualStudio = & $vswhere -latest -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $visualStudio)
{
    throw 'A Visual Studio installation with the x86 MSVC C++ build tools is required.'
}

$developerCommand = Join-Path $visualStudio 'Common7\Tools\VsDevCmd.bat'
$kernelExecutable = Join-Path $buildDirectory 'kernel.exe'
$compileCommand = 'call "' + $developerCommand +
    '" -arch=x86 -host_arch=x64 >nul && cd /d "' + $buildDirectory +
    '" && cl /nologo /c /TC /GS- /Oi /O1 /Zl /Fo:kernel.obj "' +
    (Join-Path $root 'kernel.c') +
    '" && link /nologo /nodefaultlib /machine:x86 /subsystem:native ' +
    '/entry:kernel_main /base:0x10000 /fixed /filealign:512 ' +
    '/merge:.rdata=.text /merge:.data=.text /section:.text,ERW ' +
    '/incremental:no /out:kernel.exe kernel.obj'
& $env:ComSpec /d /s /c $compileCommand
if ($LASTEXITCODE -ne 0)
{
    throw 'MSVC failed to compile or link the kernel.'
}

$bootSector = Join-Path $buildDirectory 'boot.bin'
& $nasmPath -f bin -o $bootSector (Join-Path $root 'boot.asm')
if ($LASTEXITCODE -ne 0)
{
    throw 'NASM failed to assemble the boot sector.'
}

$python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $python)
{
    throw 'Python 3 is required to package the kernel disk image.'
}
& $python.Source (Join-Path $root 'tools\make_disk.py') `
    --boot $bootSector `
    --kernel $kernelExecutable `
    --output (Join-Path $buildDirectory 'helloos.img') `
    --files-root $root
if ($LASTEXITCODE -ne 0)
{
    throw 'Failed to build the bootable disk image.'
}

Write-Output "Built $(Join-Path $buildDirectory 'helloos.img')"
