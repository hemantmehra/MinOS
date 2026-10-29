$ErrorActionPreference = 'Stop'

$image = Join-Path $PSScriptRoot 'build\helloos.img'
if (-not (Test-Path $image))
{
    throw 'Disk image not found. Run .\build.ps1 first.'
}

$qemu = Get-Command qemu-system-i386.exe -ErrorAction SilentlyContinue
if (-not $qemu -and (Test-Path 'C:\Program Files\qemu\qemu-system-i386.exe'))
{
    $qemu = Get-Item 'C:\Program Files\qemu\qemu-system-i386.exe'
}
if (-not $qemu)
{
    throw 'QEMU i386 was not found. Install QEMU or add it to PATH.'
}

$qemuPath = $qemu.Source
if (-not $qemuPath)
{
    $qemuPath = $qemu.FullName
}

& $qemuPath -machine pc -m 16M -drive "file=$image,format=raw,if=ide" `
    -boot order=c -no-reboot
if ($LASTEXITCODE -ne 0)
{
    throw "QEMU exited with code $LASTEXITCODE."
}
