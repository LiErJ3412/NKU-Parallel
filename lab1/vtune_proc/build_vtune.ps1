$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$targets = @(
    @{ Src = "matrix_naive.cpp"; Exe = "matrix_naive.exe" },
    @{ Src = "matrix_row_major.cpp"; Exe = "matrix_row_major.exe" },
    @{ Src = "sum_chain.cpp"; Exe = "sum_chain.exe" },
    @{ Src = "sum_two_way.cpp"; Exe = "sum_two_way.exe" }
)

foreach ($target in $targets) {
    Write-Host "Compiling $($target.Exe)"
    & g++ `
        -std=c++17 `
        -O3 `
        -g `
        -march=native `
        -Wall `
        -Wextra `
        -pedantic `
        $target.Src `
        -o $target.Exe
}
