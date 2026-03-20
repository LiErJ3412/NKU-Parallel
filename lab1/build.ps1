$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

New-Item -ItemType Directory -Force bin, results | Out-Null

$builds = @(
    @{ Tag = "O3"; Flags = @("-O3", "-march=native") },
    @{ Tag = "O2"; Flags = @("-O2", "-march=native") },
    @{ Tag = "O0"; Flags = @("-O0") }
)

foreach ($build in $builds) {
    $exe = "bin/arch_bench_$($build.Tag).exe"
    $args = @(
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-pedantic"
    ) + $build.Flags + @(
        "src/arch_bench.cpp",
        "-o",
        $exe
    )

    Write-Host "Compiling $($build.Tag) -> $exe"
    & g++ @args
}

Write-Host "Running full benchmark for O3"
& ".\bin\arch_bench_O3.exe" --tag O3_full

foreach ($tag in @("O0", "O2", "O3")) {
    Write-Host "Running study benchmark for $tag"
    & ".\bin\arch_bench_$tag.exe" --tag "${tag}_study" --study
}
