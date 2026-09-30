# .\com.ps1 -Port COM4

param(
   [string]$Port = ""
)

Write-Host "COM-Port Abfrage" -ForegroundColor Cyan
Write-Host "=================" -ForegroundColor Cyan

$result = change port /query

if ($Port -eq "") {
    # Zeige alle COM-Ports
    $result
} else {
    # Filtere nach spezifischem Port
    Write-Host "`nFilter: $Port" -ForegroundColor Yellow
    $result | Where-Object { $_ -match $Port }
}
