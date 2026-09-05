# SPDX-FileCopyrightText: 2026 Taskscape Ltd
# SPDX-License-Identifier: GPL-2.0-or-later
<#
.SYNOPSIS
    Builds the deterministic transfer fixtures and collects the FTP plug-in's
    per-operation measurement documents for a benchmark comparison.

.DESCRIPTION
    Implements the measurement lane described in ftp-improvements.md section 1
    and the "Benchmark and regression requirements" section.

    What this script does and does not do is deliberately explicit:

      * It CREATES the deterministic datasets, so two runs on two machines
        compare the same bytes. Content is seeded, incompressible by default,
        and reproducible from the seed alone.
      * It COLLECTS and SUMMARIZES the JSON documents the plug-in writes when
        "Collect transfer measurements" is enabled in the FTP configuration.
      * It does NOT drive the file manager's UI and does NOT open FTP
        connections itself. A transfer performed by anything other than the real
        ftp.spl would not measure the product, which is the whole point of the
        exercise, so the transfer step is performed by the operator (or by a
        separate UI-automation lane) between -Prepare and -Collect.
      * It does NOT install or configure servers, shape latency, or write
        outside the paths given to it. The self-hosted runner has no
        administrator rights, so any FTP/FTPS/SFTP endpoint and any latency
        proxy must be pre-provisioned.

    A run therefore looks like:

        .\benchmark-ftp-transfers.ps1 -Prepare -DatasetRoot D:\ftpbench
        # ... perform the copy in the file manager with measurement enabled ...
        .\benchmark-ftp-transfers.ps1 -Collect -Label "candidate-4workers"

.PARAMETER Prepare
    Create the datasets under -DatasetRoot. Existing datasets are reused when
    their manifest matches, so preparation is not part of the timed interval.

.PARAMETER Collect
    Read the measurement documents from -MetricsDirectory, summarize them, and
    append the summary to the report file.

.PARAMETER DatasetRoot
    Directory that receives the datasets. Required with -Prepare.

.PARAMETER Datasets
    Which datasets to create. Defaults to all of them.

.PARAMETER Seed
    Seed for the deterministic content. The same seed always produces the same
    bytes, so a comparison across builds is a comparison of the same work.

.PARAMETER MetricsDirectory
    Directory the plug-in writes its JSON documents to. Defaults to %TEMP%,
    which is what the plug-in uses when no directory is configured.

.PARAMETER Label
    Name recorded with the collected results, e.g. "baseline" or
    "candidate-4workers". Runs are compared by label.

.PARAMETER ReportPath
    JSON report file that accumulates the collected runs. Defaults to
    TestResults\ftp-benchmark\report.json under the repository root.

.PARAMETER Compressible
    Generate compressible content instead of incompressible content. The primary
    run must use incompressible content; compressible content is a separate
    comparison, never mixed into the same series.
#>
[CmdletBinding(DefaultParameterSetName = 'Collect')]
param(
    [Parameter(ParameterSetName = 'Prepare', Mandatory = $true)]
    [switch] $Prepare,

    [Parameter(ParameterSetName = 'Collect', Mandatory = $true)]
    [switch] $Collect,

    [Parameter(ParameterSetName = 'Prepare', Mandatory = $true)]
    [string] $DatasetRoot,

    [Parameter(ParameterSetName = 'Prepare')]
    [ValidateSet('flat10k', 'dirs1000x10', 'deep10k', 'huge100k', 'mixed', 'overwrite', 'all')]
    [string[]] $Datasets = @('all'),

    [Parameter(ParameterSetName = 'Prepare')]
    [int] $Seed = 20260904,

    [Parameter(ParameterSetName = 'Prepare')]
    [switch] $Compressible,

    [Parameter(ParameterSetName = 'Collect')]
    [string] $MetricsDirectory,

    [Parameter(ParameterSetName = 'Collect')]
    [string] $Label = 'unlabelled',

    [Parameter(ParameterSetName = 'Collect')]
    [string] $ReportPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot

# ---------------------------------------------------------------------------
# Deterministic content
#
# A xorshift generator is used rather than System.Random so the bytes depend
# only on the seed, not on the .NET version running the script. Two machines
# comparing a baseline against a candidate must transfer identical bytes, or the
# comparison measures the data instead of the code.
# ---------------------------------------------------------------------------
function New-DeterministicBytes {
    param(
        [Parameter(Mandatory = $true)][int] $Length,
        [Parameter(Mandatory = $true)][uint32] $Seed,
        [bool] $MakeCompressible = $false
    )

    $bytes = [byte[]]::new($Length)
    if ($MakeCompressible) {
        # A short repeating pattern compresses well, which is what the separate
        # compressible-content comparison needs.
        $pattern = [System.Text.Encoding]::ASCII.GetBytes('The quick brown fox jumps over the lazy dog. ')
        for ($i = 0; $i -lt $Length; $i++) {
            $bytes[$i] = $pattern[$i % $pattern.Length]
        }
        return $bytes
    }

    $state = $Seed
    if ($state -eq 0) { $state = 2463534242 }
    for ($i = 0; $i -lt $Length; $i++) {
        # xorshift32
        $state = $state -bxor (($state -shl 13) -band 0xFFFFFFFF)
        $state = $state -bxor ($state -shr 17)
        $state = $state -bxor (($state -shl 5) -band 0xFFFFFFFF)
        $bytes[$i] = [byte]($state -band 0xFF)
    }
    return $bytes
}

function Write-DeterministicFile {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][int] $Length,
        [Parameter(Mandatory = $true)][uint32] $Seed,
        [bool] $MakeCompressible = $false
    )

    $bytes = New-DeterministicBytes -Length $Length -Seed $Seed -MakeCompressible $MakeCompressible
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

function New-DatasetDirectory {
    param([Parameter(Mandatory = $true)][string] $Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

# Each dataset writes a manifest. A dataset whose manifest already matches is
# left alone: rebuilding it would put minutes of local disk work inside a
# measurement window it has nothing to do with.
function Test-DatasetCurrent {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][hashtable] $Manifest
    )

    $manifestPath = Join-Path $Path '.benchmark-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath)) { return $false }
    try {
        $existing = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    } catch {
        return $false
    }
    foreach ($key in $Manifest.Keys) {
        if (-not ($existing.PSObject.Properties.Name -contains $key)) { return $false }
        if ("$($existing.$key)" -ne "$($Manifest[$key])") { return $false }
    }
    return $true
}

function Save-DatasetManifest {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][hashtable] $Manifest
    )
    $manifestPath = Join-Path $Path '.benchmark-manifest.json'
    $Manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}

function New-FlatDataset {
    param([string] $Root, [uint32] $Seed, [bool] $MakeCompressible)

    # One flat directory of 10,000 deterministic 4 KiB files: isolates per-file
    # command, TLS, allocation and UI overhead from payload throughput.
    $path = Join-Path $Root 'flat10k'
    $manifest = @{ dataset = 'flat10k'; files = 10000; size = 4096; seed = $Seed; compressible = $MakeCompressible }
    if (Test-DatasetCurrent -Path $path -Manifest $manifest) {
        Write-Host "flat10k: already current, reused"
        return
    }
    New-DatasetDirectory -Path $path
    for ($i = 0; $i -lt 10000; $i++) {
        $file = Join-Path $path ("file{0:D5}.bin" -f $i)
        Write-DeterministicFile -Path $file -Length 4096 -Seed ([uint32]($Seed + $i)) -MakeCompressible $MakeCompressible
        if (($i % 1000) -eq 0) { Write-Host "flat10k: $i / 10000" }
    }
    Save-DatasetManifest -Path $path -Manifest $manifest
}

function New-DirsDataset {
    param([string] $Root, [uint32] $Seed, [bool] $MakeCompressible)

    # 1,000 directories of 10 files each, plus empty directories: measures
    # listing count, dependency ordering and time to first transfer.
    $path = Join-Path $Root 'dirs1000x10'
    $manifest = @{ dataset = 'dirs1000x10'; dirs = 1000; filesPerDir = 10; emptyDirs = 50; size = 4096; seed = $Seed; compressible = $MakeCompressible }
    if (Test-DatasetCurrent -Path $path -Manifest $manifest) {
        Write-Host "dirs1000x10: already current, reused"
        return
    }
    New-DatasetDirectory -Path $path
    for ($d = 0; $d -lt 1000; $d++) {
        $dir = Join-Path $path ("dir{0:D4}" -f $d)
        New-DatasetDirectory -Path $dir
        for ($f = 0; $f -lt 10; $f++) {
            $file = Join-Path $dir ("file{0:D2}.bin" -f $f)
            Write-DeterministicFile -Path $file -Length 4096 -Seed ([uint32]($Seed + $d * 10 + $f)) -MakeCompressible $MakeCompressible
        }
        if (($d % 100) -eq 0) { Write-Host "dirs1000x10: $d / 1000" }
    }
    # Empty directories must survive the copy too, and they are a common source
    # of "finished with 0 items" bugs in dependency ordering.
    for ($e = 0; $e -lt 50; $e++) {
        New-DatasetDirectory -Path (Join-Path $path ("empty{0:D3}" -f $e))
    }
    Save-DatasetManifest -Path $path -Manifest $manifest
}

function New-DeepDataset {
    param([string] $Root, [uint32] $Seed, [bool] $MakeCompressible)

    # A deep tree with 10,000 files inside supported path limits: traversal
    # fairness, cycle checks and directory finalization order.
    $path = Join-Path $Root 'deep10k'
    $manifest = @{ dataset = 'deep10k'; depth = 20; files = 10000; size = 4096; seed = $Seed; compressible = $MakeCompressible }
    if (Test-DatasetCurrent -Path $path -Manifest $manifest) {
        Write-Host "deep10k: already current, reused"
        return
    }
    New-DatasetDirectory -Path $path
    # 20 levels x 500 files keeps every full path well inside MAX_PATH, so a
    # failure in this dataset is a scheduler failure and not a path-length one.
    $current = $path
    for ($level = 0; $level -lt 20; $level++) {
        $current = Join-Path $current ("lvl{0:D2}" -f $level)
        New-DatasetDirectory -Path $current
        for ($f = 0; $f -lt 500; $f++) {
            $file = Join-Path $current ("f{0:D3}.bin" -f $f)
            Write-DeterministicFile -Path $file -Length 4096 -Seed ([uint32]($Seed + $level * 500 + $f)) -MakeCompressible $MakeCompressible
        }
        Write-Host "deep10k: level $level / 20"
    }
    Save-DatasetManifest -Path $path -Manifest $manifest
}

function New-HugeDataset {
    param([string] $Root, [uint32] $Seed, [bool] $MakeCompressible)

    # More than 100,000 small files: exercises queue backpressure, retained
    # history and the operation queue's defensive bounds.
    $path = Join-Path $Root 'huge100k'
    $manifest = @{ dataset = 'huge100k'; files = 120000; size = 1024; seed = $Seed; compressible = $MakeCompressible }
    if (Test-DatasetCurrent -Path $path -Manifest $manifest) {
        Write-Host "huge100k: already current, reused"
        return
    }
    New-DatasetDirectory -Path $path
    # Spread across 120 directories so the local file system is not itself the
    # bottleneck being measured.
    for ($d = 0; $d -lt 120; $d++) {
        $dir = Join-Path $path ("d{0:D3}" -f $d)
        New-DatasetDirectory -Path $dir
        for ($f = 0; $f -lt 1000; $f++) {
            $file = Join-Path $dir ("f{0:D4}.bin" -f $f)
            Write-DeterministicFile -Path $file -Length 1024 -Seed ([uint32]($Seed + $d * 1000 + $f)) -MakeCompressible $MakeCompressible
        }
        Write-Host "huge100k: $d / 120 directories"
    }
    Save-DatasetManifest -Path $path -Manifest $manifest
}

function New-MixedDataset {
    param([string] $Root, [uint32] $Seed, [bool] $MakeCompressible)

    # Empty, 1 KiB, 64 KiB and 1 MiB files plus one 1 GiB file: fairness between
    # workers and the payload-throughput control case.
    $path = Join-Path $Root 'mixed'
    $manifest = @{ dataset = 'mixed'; seed = $Seed; compressible = $MakeCompressible }
    if (Test-DatasetCurrent -Path $path -Manifest $manifest) {
        Write-Host "mixed: already current, reused"
        return
    }
    New-DatasetDirectory -Path $path
    for ($i = 0; $i -lt 100; $i++) {
        Write-DeterministicFile -Path (Join-Path $path ("empty{0:D3}.bin" -f $i)) -Length 0 -Seed ([uint32]($Seed + $i)) -MakeCompressible $MakeCompressible
    }
    for ($i = 0; $i -lt 500; $i++) {
        Write-DeterministicFile -Path (Join-Path $path ("small{0:D3}.bin" -f $i)) -Length 1024 -Seed ([uint32]($Seed + 1000 + $i)) -MakeCompressible $MakeCompressible
    }
    for ($i = 0; $i -lt 100; $i++) {
        Write-DeterministicFile -Path (Join-Path $path ("medium{0:D3}.bin" -f $i)) -Length (64 * 1024) -Seed ([uint32]($Seed + 2000 + $i)) -MakeCompressible $MakeCompressible
    }
    for ($i = 0; $i -lt 20; $i++) {
        Write-DeterministicFile -Path (Join-Path $path ("large{0:D3}.bin" -f $i)) -Length (1024 * 1024) -Seed ([uint32]($Seed + 3000 + $i)) -MakeCompressible $MakeCompressible
    }

    # The 1 GiB file is written in chunks: a single 1 GiB byte array would make
    # dataset preparation fail on memory rather than on disk space.
    $hugePath = Join-Path $path 'huge-1gib.bin'
    Write-Host "mixed: writing huge-1gib.bin"
    $chunk = New-DeterministicBytes -Length (4 * 1024 * 1024) -Seed ([uint32]($Seed + 4000)) -MakeCompressible $MakeCompressible
    $stream = [System.IO.File]::Create($hugePath)
    try {
        for ($i = 0; $i -lt 256; $i++) { $stream.Write($chunk, 0, $chunk.Length) }
    } finally {
        $stream.Dispose()
    }
    Save-DatasetManifest -Path $path -Manifest $manifest
}

function New-OverwriteDataset {
    param([string] $Root, [uint32] $Seed, [bool] $MakeCompressible)

    # A destination with a fixed mixture of identical names, different sizes and
    # partial files: exercises overwrite, skip, resume and cache invalidation.
    # It is generated from the flat10k names so the collision set is exact.
    $path = Join-Path $Root 'overwrite-destination'
    $manifest = @{ dataset = 'overwrite'; identical = 200; differentSize = 200; partial = 200; seed = $Seed; compressible = $MakeCompressible }
    if (Test-DatasetCurrent -Path $path -Manifest $manifest) {
        Write-Host "overwrite: already current, reused"
        return
    }
    New-DatasetDirectory -Path $path
    for ($i = 0; $i -lt 200; $i++) {
        # identical content and name: must be detected as unchanged
        Write-DeterministicFile -Path (Join-Path $path ("file{0:D5}.bin" -f $i)) -Length 4096 -Seed ([uint32]($Seed + $i)) -MakeCompressible $MakeCompressible
    }
    for ($i = 200; $i -lt 400; $i++) {
        # same name, different size: must not be resumed from a stale size
        Write-DeterministicFile -Path (Join-Path $path ("file{0:D5}.bin" -f $i)) -Length 8192 -Seed ([uint32]($Seed + $i)) -MakeCompressible $MakeCompressible
    }
    for ($i = 400; $i -lt 600; $i++) {
        # same name, truncated content: the resume candidate
        Write-DeterministicFile -Path (Join-Path $path ("file{0:D5}.bin" -f $i)) -Length 1024 -Seed ([uint32]($Seed + $i)) -MakeCompressible $MakeCompressible
    }
    Save-DatasetManifest -Path $path -Manifest $manifest
}

function Invoke-Prepare {
    New-DatasetDirectory -Path $DatasetRoot
    $wanted = if ($Datasets -contains 'all') {
        @('flat10k', 'dirs1000x10', 'deep10k', 'huge100k', 'mixed', 'overwrite')
    } else {
        $Datasets
    }

    $seedValue = [uint32] $Seed
    $compressible = [bool] $Compressible
    foreach ($dataset in $wanted) {
        switch ($dataset) {
            'flat10k'      { New-FlatDataset      -Root $DatasetRoot -Seed $seedValue -MakeCompressible $compressible }
            'dirs1000x10'  { New-DirsDataset      -Root $DatasetRoot -Seed $seedValue -MakeCompressible $compressible }
            'deep10k'      { New-DeepDataset      -Root $DatasetRoot -Seed $seedValue -MakeCompressible $compressible }
            'huge100k'     { New-HugeDataset      -Root $DatasetRoot -Seed $seedValue -MakeCompressible $compressible }
            'mixed'        { New-MixedDataset     -Root $DatasetRoot -Seed $seedValue -MakeCompressible $compressible }
            'overwrite'    { New-OverwriteDataset -Root $DatasetRoot -Seed $seedValue -MakeCompressible $compressible }
        }
    }

    Write-Host ''
    Write-Host 'Datasets ready under' $DatasetRoot
    Write-Host 'Next: enable "Collect transfer measurements" on the FTP plug-in''s'
    Write-Host 'Advanced configuration page, perform the copy, then run this script'
    Write-Host 'again with -Collect.'
}

# ---------------------------------------------------------------------------
# Collection
# ---------------------------------------------------------------------------
function Invoke-Collect {
    if (-not $MetricsDirectory) { $MetricsDirectory = $env:TEMP }
    if (-not $ReportPath) {
        $ReportPath = Join-Path $repositoryRoot 'TestResults\ftp-benchmark\report.json'
    }

    if (-not (Test-Path -LiteralPath $MetricsDirectory)) {
        throw "Metrics directory not found: $MetricsDirectory"
    }

    # Wrapped in @() so a single document still exposes .Count - an unwrapped
    # scalar would make a one-run collection fail instead of reporting it.
    $documents = @(Get-ChildItem -LiteralPath $MetricsDirectory -Filter 'ftp-metrics-*.json' -File |
                   Sort-Object LastWriteTime)

    if ($documents.Count -eq 0) {
        # An empty result is reported as such rather than as a passing run with
        # no data: "no measurement" and "measured zero" are different outcomes.
        Write-Warning "No measurement documents found in $MetricsDirectory."
        Write-Warning 'Check that "Collect transfer measurements" is enabled on the FTP plug-in''s Advanced configuration page.'
        return
    }

    $runs = @()
    foreach ($document in $documents) {
        try {
            $parsed = Get-Content -LiteralPath $document.FullName -Raw | ConvertFrom-Json
        } catch {
            Write-Warning "Skipping unreadable document $($document.Name): $_"
            continue
        }
        $runs += [pscustomobject]@{
            file                = $document.Name
            operation           = $parsed.operation
            host                = $parsed.target.host
            security            = $parsed.target.security
            architecture        = $parsed.build.architecture
            configuration       = $parsed.build.configuration
            totalMs             = $parsed.phases_ms.total
            discoveryMs         = $parsed.phases_ms.discovery
            toFirstByteMs       = $parsed.phases_ms.to_first_payload_byte
            toFirstFileMs       = $parsed.phases_ms.to_first_completed_file
            filesCompleted      = $parsed.throughput.files_completed
            filesFailed         = $parsed.throughput.files_failed
            filesPerSecond      = $parsed.throughput.files_per_second
            payloadBytesPerSec  = $parsed.throughput.payload_bytes_per_second
            p50LatencyMs        = $parsed.file_latency_ms.p50_upper_bound
            p95LatencyMs        = $parsed.file_latency_ms.p95_upper_bound
            logins              = $parsed.connections.logins
            workerPeak          = $parsed.connections.worker_peak
            admissionDenials    = $parsed.connections.admission_denials
            serverRefusals      = $parsed.connections.server_refusals
            listingsCompleted   = $parsed.listings.completed
            listingsFailed      = $parsed.listings.failed
            queueHighWaterMark  = $parsed.scheduler.queue_high_water_mark
            queueRejectedItems  = $parsed.scheduler.queue_rejected_items
            uidLookupScanned    = $parsed.scheduler.uid_lookup_scanned_items
            tlsFull             = $parsed.tls_handshakes.full.count
            tlsResumed          = $parsed.tls_handshakes.resumed.count
            tlsUnknown          = $parsed.tls_handshakes.unknown.count
            bufferAllocations   = $parsed.local.buffer_allocations
        }
    }

    if ($runs.Count -eq 0) {
        Write-Warning 'No readable measurement documents.'
        return
    }

    # Median and range, as the plan requires - a mean would hide the outlier a
    # single stalled connection produces.
    function Get-Median {
        param([double[]] $Values)
        if ($Values.Count -eq 0) { return 0 }
        # Sort-Object returns a scalar for a single element, so wrap it: a
        # one-run series must still produce a median rather than an error.
        $sorted = @($Values | Sort-Object)
        $middle = [int]([math]::Floor($sorted.Count / 2))
        if ($sorted.Count % 2 -eq 1) { return $sorted[$middle] }
        return ($sorted[$middle - 1] + $sorted[$middle]) / 2
    }

    $filesPerSecond = @($runs | ForEach-Object { [double]$_.filesPerSecond })
    $totalMs        = @($runs | ForEach-Object { [double]$_.totalMs })

    $summary = [pscustomobject]@{
        label                  = $Label
        collectedAtUtc         = (Get-Date).ToUniversalTime().ToString('o')
        runCount               = $runs.Count
        medianFilesPerSecond   = Get-Median -Values $filesPerSecond
        minFilesPerSecond      = ($filesPerSecond | Measure-Object -Minimum).Minimum
        maxFilesPerSecond      = ($filesPerSecond | Measure-Object -Maximum).Maximum
        medianTotalMs          = Get-Median -Values $totalMs
        minTotalMs             = ($totalMs | Measure-Object -Minimum).Minimum
        maxTotalMs             = ($totalMs | Measure-Object -Maximum).Maximum
        totalFilesFailed       = ($runs | Measure-Object -Property filesFailed -Sum).Sum
        totalQueueRejected     = ($runs | Measure-Object -Property queueRejectedItems -Sum).Sum
        totalAdmissionDenials  = ($runs | Measure-Object -Property admissionDenials -Sum).Sum
        totalServerRefusals    = ($runs | Measure-Object -Property serverRefusals -Sum).Sum
        tlsResumedHandshakes   = ($runs | Measure-Object -Property tlsResumed -Sum).Sum
        tlsFullHandshakes      = ($runs | Measure-Object -Property tlsFull -Sum).Sum
        tlsUnknownHandshakes   = ($runs | Measure-Object -Property tlsUnknown -Sum).Sum
        runs                   = $runs
    }

    Write-Host ''
    Write-Host "Label:                 $($summary.label)"
    Write-Host "Runs collected:        $($summary.runCount)"
    Write-Host ("Files/s (median):      {0:N2}  [{1:N2} .. {2:N2}]" -f $summary.medianFilesPerSecond, $summary.minFilesPerSecond, $summary.maxFilesPerSecond)
    Write-Host ("Total ms (median):     {0:N0}  [{1:N0} .. {2:N0}]" -f $summary.medianTotalMs, $summary.minTotalMs, $summary.maxTotalMs)
    Write-Host "Files failed:          $($summary.totalFilesFailed)"
    Write-Host "Queue items rejected:  $($summary.totalQueueRejected)"
    Write-Host "Admission denials:     $($summary.totalAdmissionDenials)"
    Write-Host "Server refusals:       $($summary.totalServerRefusals)"
    Write-Host "TLS resumed / full / unknown: $($summary.tlsResumedHandshakes) / $($summary.tlsFullHandshakes) / $($summary.tlsUnknownHandshakes)"
    if ($summary.tlsUnknownHandshakes -gt 0) {
        # Reported explicitly: an unknown resumption result is not evidence of a
        # full handshake, and it is not evidence of a resumed one either.
        Write-Host 'NOTE: some handshakes reported an unknown resumption status; corroborate with server logs.'
    }

    $reportDirectory = Split-Path -Parent $ReportPath
    if ($reportDirectory -and -not (Test-Path -LiteralPath $reportDirectory)) {
        New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
    }

    $report = @()
    if (Test-Path -LiteralPath $ReportPath) {
        try {
            $existing = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
            if ($existing) { $report = @($existing) }
        } catch {
            Write-Warning "Existing report could not be parsed and will be replaced: $_"
        }
    }
    $report += $summary
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
    Write-Host ''
    Write-Host "Report written to $ReportPath"
}

if ($Prepare) { Invoke-Prepare } else { Invoke-Collect }
