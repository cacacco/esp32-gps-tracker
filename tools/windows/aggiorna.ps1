<#
    aggiorna.ps1 - scarica le novita' dal repository e ricarica il firmware.

    Doppio clic su aggiorna.bat. In tre passi:
      1. mette da parte le tue modifiche locali, se ce ne sono
      2. scarica gli aggiornamenti e ti elenca cosa e' cambiato
      3. compila e carica, poi apre il log seriale

    Serve git installato: https://git-scm.com/download/win
#>
[CmdletBinding()]
param(
    [string]$Porta,
    [switch]$SoloScarica   # aggiorna i file senza caricare il firmware
)

$ErrorActionPreference = 'Stop'

function Scrivi([string]$testo, [string]$colore = 'Gray') {
    Write-Host $testo -ForegroundColor $colore
}

$progetto = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ramo     = 'esp32-gps-tracker'

Scrivi ""
Scrivi "=== Aggiornamento ESP32 GPS Tracker ===" 'Cyan'
Scrivi "progetto: $progetto"

# --- git c'e'? -------------------------------------------------------------
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Scrivi ""
    Scrivi "git non e' installato." 'Red'
    Scrivi "Scaricalo da https://git-scm.com/download/win, poi chiudi e riapri questa finestra." 'Red'
    exit 1
}

# --- siamo in un clone, e sul ramo giusto? ---------------------------------
$ramoCorrente = (& git -C $progetto rev-parse --abbrev-ref HEAD 2>$null)
if ($LASTEXITCODE -ne 0) {
    Scrivi ""
    Scrivi "Questa cartella non e' un clone git: l'aggiornamento automatico non e' possibile." 'Red'
    Scrivi "Succede se hai estratto lo ZIP invece di usare 'git clone'." 'Red'
    exit 1
}
Scrivi "ramo: $ramoCorrente"

if ($ramoCorrente -ne $ramo) {
    Scrivi ""
    Scrivi "Sei sul ramo '$ramoCorrente', il progetto sta su '$ramo'." 'Yellow'
    $risposta = Read-Host "Passo a '$ramo'? [S/n]"
    if ($risposta -eq '' -or $risposta -match '^[sSyY]') {
        & git -C $progetto checkout $ramo
        if ($LASTEXITCODE -ne 0) { Scrivi "Cambio ramo non riuscito." 'Red'; exit 1 }
    } else {
        exit 1
    }
}

$prima = (& git -C $progetto rev-parse --short HEAD)

# --- modifiche locali: le metto da parte e le rimetto dopo -----------------
$modificati = & git -C $progetto status --porcelain
$messoDaParte = $false
if ($modificati) {
    Scrivi ""
    Scrivi "Hai modifiche locali (probabilmente config.h):" 'Yellow'
    $modificati | ForEach-Object { Scrivi "  $_" }
    Scrivi "Le metto da parte e le rimetto dopo l'aggiornamento."
    & git -C $progetto stash push -m "aggiorna.ps1 $(Get-Date -Format s)" | Out-Null
    if ($LASTEXITCODE -ne 0) { Scrivi "Non sono riuscito a metterle da parte." 'Red'; exit 1 }
    $messoDaParte = $true
}

# --- scarico ---------------------------------------------------------------
Scrivi ""
Scrivi "Scarico gli aggiornamenti..." 'Cyan'
& git -C $progetto pull --ff-only
if ($LASTEXITCODE -ne 0) {
    Scrivi "Scaricamento non riuscito." 'Red'
    if ($messoDaParte) { & git -C $progetto stash pop | Out-Null }
    exit 1
}

$dopo = (& git -C $progetto rev-parse --short HEAD)

if ($messoDaParte) {
    Scrivi ""
    Scrivi "Rimetto le tue modifiche..." 'Cyan'
    & git -C $progetto stash pop
    if ($LASTEXITCODE -ne 0) {
        Scrivi ""
        Scrivi "Le tue modifiche sono in conflitto con quelle nuove." 'Red'
        Scrivi "Sono al sicuro: le recuperi con 'git stash pop' dopo aver risolto." 'Red'
        Scrivi "Se vuoi buttarle e ripartire dalla versione pulita:" 'Red'
        Scrivi "  git -C `"$progetto`" checkout -- ." 'Red'
        exit 1
    }
}

# --- cosa e' cambiato ------------------------------------------------------
Scrivi ""
if ($prima -eq $dopo) {
    Scrivi "Niente di nuovo: hai gia' l'ultima versione ($dopo)." 'Green'
    if ($SoloScarica) { exit 0 }
    $risposta = Read-Host "Ricarico lo stesso il firmware sulla scheda? [s/N]"
    if ($risposta -notmatch '^[sSyY]') { exit 0 }
} else {
    Scrivi "Aggiornato da $prima a $dopo. Novita':" 'Green'
    & git -C $progetto log --oneline "$prima..$dopo" | ForEach-Object { Scrivi "  $_" }
    if ($SoloScarica) {
        Scrivi ""
        Scrivi "File aggiornati. Per caricare il firmware usa carica-firmware.bat." 'Cyan'
        exit 0
    }
}

# --- carico ----------------------------------------------------------------
Scrivi ""
Scrivi "Compilo e carico sulla scheda..." 'Cyan'
# Tabella e non lista: con una lista PowerShell assegna i valori per posizione
# e '-Auto' finirebbe dentro -Porta, cioe' nel nome della porta seriale.
$opzioni = @{ Auto = $true }
if ($Porta) { $opzioni['Porta'] = $Porta }
& "$PSScriptRoot\carica-firmware.ps1" @opzioni
exit $LASTEXITCODE
