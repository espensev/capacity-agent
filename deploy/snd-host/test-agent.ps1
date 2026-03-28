# test-agent.ps1 — Test all capacity-agent endpoints and save results
# Run this WHILE start-agent.ps1 is running in another terminal.
# Results are saved to test-results.txt in the same directory.

param(
    [string]$Host_ = "127.0.0.1",
    [int]$Port = 8091
)

$BaseUrl = "http://${Host_}:${Port}"
$OutFile = Join-Path $PSScriptRoot "test-results.txt"

function Test-Endpoint {
    param([string]$Path, [string]$Label)

    Write-Host "  Testing $Label ... " -NoNewline
    try {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $response = Invoke-WebRequest -Uri "$BaseUrl$Path" -UseBasicParsing -TimeoutSec 5
        $sw.Stop()
        $status = $response.StatusCode
        $body = $response.Content
        Write-Host "OK (${status}, $($sw.ElapsedMilliseconds)ms)" -ForegroundColor Green

        return @{
            endpoint = $Path
            label    = $Label
            status   = $status
            latency  = "$($sw.ElapsedMilliseconds)ms"
            body     = $body
        }
    }
    catch {
        Write-Host "FAIL: $($_.Exception.Message)" -ForegroundColor Red
        return @{
            endpoint = $Path
            label    = $Label
            status   = "ERROR"
            latency  = "N/A"
            body     = $_.Exception.Message
        }
    }
}

Write-Host ""
Write-Host "capacity-agent endpoint tests" -ForegroundColor Cyan
Write-Host "Target: $BaseUrl"
Write-Host "Time:   $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Write-Host ""

$results = @()
$results += Test-Endpoint "/v1/status"    "Status"
$results += Test-Endpoint "/v1/snapshot"  "Snapshot"
$results += Test-Endpoint "/v1/readiness" "Readiness"
$results += Test-Endpoint "/v1/readiness?required_vram_bytes=8000000000" "Readiness (8GB)"
$results += Test-Endpoint "/v1/catalog"   "Catalog"
$results += Test-Endpoint "/v1/history?sensor_uid=test&limit=1" "History"

# Write results to file
$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("=== capacity-agent test results ===")
[void]$sb.AppendLine("Target: $BaseUrl")
[void]$sb.AppendLine("Time:   $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
[void]$sb.AppendLine("Machine: $env:COMPUTERNAME")
[void]$sb.AppendLine("")

foreach ($r in $results) {
    [void]$sb.AppendLine("--- $($r.label) [$($r.endpoint)] ---")
    [void]$sb.AppendLine("Status:  $($r.status)")
    [void]$sb.AppendLine("Latency: $($r.latency)")
    [void]$sb.AppendLine("Body:")
    # Pretty-print JSON if possible
    try {
        $pretty = $r.body | ConvertFrom-Json | ConvertTo-Json -Depth 10
        [void]$sb.AppendLine($pretty)
    }
    catch {
        [void]$sb.AppendLine($r.body)
    }
    [void]$sb.AppendLine("")
}

$sb.ToString() | Out-File -FilePath $OutFile -Encoding utf8
Write-Host ""
Write-Host "Results saved to: $OutFile" -ForegroundColor Green
Write-Host ""

# Summary
$passed = ($results | Where-Object { $_.status -eq 200 }).Count
$total  = $results.Count
$color  = if ($passed -eq $total) { "Green" } else { "Yellow" }
Write-Host "Result: $passed/$total endpoints OK" -ForegroundColor $color
