# start-agent.ps1 — Start capacity-agent on SND-HOST
# Run this in an elevated PowerShell terminal.
# The agent stays in the foreground so you can see output. Ctrl+C to stop.

$AgentRoot = "D:\Development\capacity-agent"
$Bin       = "$AgentRoot\bin\host_agent.exe"
$Schema    = "$AgentRoot\sql\schema.sql"
$Db        = "$AgentRoot\data\runtime\host_agent.db"

if (-not (Test-Path $Bin)) {
    Write-Host "ERROR: $Bin not found. Deploy the binary first." -ForegroundColor Red
    exit 1
}

# Ensure runtime directory exists
New-Item -ItemType Directory -Force -Path "$AgentRoot\data\runtime" | Out-Null

Write-Host "Starting capacity-agent..." -ForegroundColor Cyan
Write-Host "  Machine ID : snd-host"
Write-Host "  API        : http://0.0.0.0:8091"
Write-Host "  Schema     : $Schema"
Write-Host "  Database   : $Db"
Write-Host "  GPU        : enabled"
Write-Host "  Ollama     : disabled (no local Ollama)"
Write-Host ""
Write-Host "Press Ctrl+C to stop." -ForegroundColor Yellow
Write-Host ""

& $Bin `
    --machine-id snd-host `
    --schema-path $Schema `
    --db-path $Db `
    --api-bind 0.0.0.0 `
    --api-port 8091 `
    --no-ollama
