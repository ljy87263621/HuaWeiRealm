[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ListenAddress,
    [Parameter(Mandatory = $true)]
    [string]$AllowedRemoteAddress,
    [int]$Port = 8888
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell session.'
}

if(-not (Get-NetIPAddress -AddressFamily IPv4 -IPAddress $ListenAddress -ErrorAction SilentlyContinue)) {
    throw "The requested LAN address is not assigned to this computer: $ListenAddress"
}

if(-not (Test-NetConnection -ComputerName '127.0.0.1' -Port $Port -InformationLevel Quiet)) {
    throw "WSL localhost relay is not listening on 127.0.0.1:$Port. Start the WSL server first."
}

& netsh interface portproxy delete v4tov4 listenaddress=$ListenAddress listenport=$Port | Out-Null
& netsh interface portproxy add v4tov4 listenaddress=$ListenAddress listenport=$Port `
    connectaddress=127.0.0.1 connectport=$Port
if($LASTEXITCODE -ne 0) {
    throw 'Unable to create the Windows TCP port proxy.'
}

$ruleName = "LVGL Chat Server $Port (LAN)"
Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Action Allow -Enabled True `
    -Profile Private -Protocol TCP -LocalAddress $ListenAddress -LocalPort $Port `
    -RemoteAddress $AllowedRemoteAddress | Out-Null

Write-Host "Board endpoint ready: $ListenAddress`:$Port (allowed: $AllowedRemoteAddress)"
