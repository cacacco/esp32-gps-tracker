<#
    carica-firmware.ps1 - compila e carica il firmware dell'ESP32 GPS Tracker.

    Si usa con un doppio clic su carica-firmware.bat, che lo lancia senza
    toccare i criteri di esecuzione di PowerShell. Da riga di comando:

        .\carica-firmware.ps1                  compila, carica e apre il log
        .\carica-firmware.ps1 -SoloMonitor     apre solo il log seriale
        .\carica-firmware.ps1 -SoloCompila     compila senza caricare
        .\carica-firmware.ps1 -Pulisci         ricompila tutto da zero
        .\carica-firmware.ps1 -Porta COM7      forza la porta seriale

    Se PlatformIO non c'e', lo installa (serve Python 3 sul PC).
#>
[CmdletBinding()]
param(
    [string]$Porta,
    [switch]$SoloMonitor,
    [switch]$SoloCompila,
    [switch]$SenzaMonitor,
    [switch]$Pulisci,
    [switch]$Auto
)

$ErrorActionPreference = 'Stop'

function Scrivi([string]$testo, [string]$colore = 'Gray') {
    Write-Host $testo -ForegroundColor $colore
}

function Trova-Pio {
    $cmd = Get-Command pio -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    # Percorso dell'installatore ufficiale, che si crea un ambiente tutto suo
    $suo = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
    if (Test-Path $suo) { return $suo }

    return $null
}

function Trova-Python {
    foreach ($nome in @('py', 'python', 'python3')) {
        $cmd = Get-Command $nome -ErrorAction SilentlyContinue
        if (-not $cmd) { continue }
        # Lo stub del Microsoft Store risponde al nome ma non esegue niente:
        # l'unico modo di distinguerlo e' chiedergli la versione.
        $versione = & $cmd.Source --version 2>&1
        if ($LASTEXITCODE -eq 0 -and "$versione" -match 'Python 3') { return $cmd.Source }
    }
    return $null
}

function Installa-Pio {
    Scrivi "PlatformIO non risulta installato." 'Yellow'

    $python = Trova-Python
    if (-not $python) {
        Scrivi ""
        Scrivi "Serve prima Python 3. Scaricalo da https://www.python.org/downloads/" 'Red'
        Scrivi "e durante l'installazione spunta 'Add python.exe to PATH'." 'Red'
        Scrivi "Poi rilancia questo script." 'Red'
        throw "Python non trovato"
    }

    Scrivi "Installo PlatformIO con $python (un paio di minuti)..." 'Cyan'
    $installatore = Join-Path $env:TEMP 'get-platformio.py'
    Invoke-WebRequest -UseBasicParsing `
        -Uri 'https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py' `
        -OutFile $installatore
    & $python $installatore
    if ($LASTEXITCODE -ne 0) { throw "installazione di PlatformIO fallita" }

    $pio = Trova-Pio
    if (-not $pio) { throw "PlatformIO installato ma non trovato: chiudi e riapri la finestra, poi riprova" }
    return $pio
}

# --- cartella del progetto -------------------------------------------------
# Lo script sta in tools\windows\, quindi il progetto e' due livelli sopra.
$progetto = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ini      = Join-Path $progetto 'platformio.ini'

Scrivi ""
Scrivi "=== ESP32 GPS Tracker ===" 'Cyan'
Scrivi "progetto: $progetto"

if (-not (Test-Path $ini)) {
    Scrivi ""
    Scrivi "Non trovo platformio.ini in $progetto" 'Red'
    Scrivi "Lo script deve restare dentro la cartella del progetto, in tools\windows\." 'Red'
    exit 1
}

# --- menu, se lanciato senza opzioni (tipico del doppio clic) --------------
if (-not ($Auto -or $SoloMonitor -or $SoloCompila -or $SenzaMonitor -or $Pulisci -or $Porta)) {
    Scrivi ""
    Scrivi "Cosa vuoi fare?"
    Scrivi "  [1] Compila, carica e apri il log seriale   (predefinito)"
    Scrivi "  [2] Solo log seriale"
    Scrivi "  [3] Solo compilazione"
    Scrivi "  [4] Pulisci e ricompila da zero"
    $scelta = Read-Host "Scelta [1]"
    switch ($scelta) {
        '2' { $SoloMonitor = $true }
        '3' { $SoloCompila = $true }
        '4' { $Pulisci     = $true }
    }
}

# --- PlatformIO ------------------------------------------------------------
$pio = Trova-Pio
if (-not $pio) { $pio = Installa-Pio }
Scrivi "platformio: $pio"

Push-Location $progetto
try {
    if ($Pulisci) {
        Scrivi ""
        Scrivi "Pulizia..." 'Cyan'
        & $pio run -t fullclean
    }

    if ($SoloMonitor) {
        Scrivi ""
        Scrivi "Log seriale: esci con Ctrl+C" 'Cyan'
        $argomenti = @('device', 'monitor')
        if ($Porta) { $argomenti += @('-p', $Porta) }
        & $pio @argomenti
        exit $LASTEXITCODE
    }

    # La prima compilazione scarica toolchain e librerie: diverse centinaia di MB.
    Scrivi ""
    Scrivi "Compilazione... (la prima volta scarica il necessario, anche 10 minuti)" 'Cyan'
    $argomenti = @('run')
    if (-not $SoloCompila) {
        $argomenti += @('-t', 'upload')
        if (-not $SenzaMonitor) { $argomenti += @('-t', 'monitor') }
        if ($Porta) { $argomenti += @('--upload-port', $Porta, '--monitor-port', $Porta) }
    }

    & $pio @argomenti
    $esito = $LASTEXITCODE

    if ($esito -ne 0) {
        Scrivi ""
        Scrivi "Qualcosa non ha funzionato (codice $esito). Le cause tipiche:" 'Yellow'
        Scrivi "  - la scheda non entra in modalita' caricamento: tieni premuto BOOT"
        Scrivi "    quando compare 'Connecting....' e rilascia appena inizia a scrivere"
        Scrivi "  - un altro programma tiene occupata la porta (un monitor seriale"
        Scrivi "    rimasto aperto, u-center): chiudilo e riprova"
        Scrivi "  - porta sbagliata: guarda l'elenco qui sotto e rilancia con -Porta COMx"
        Scrivi "  - cavo USB di sola ricarica, senza i fili dati"
        Scrivi ""
        & $pio device list
    }

    exit $esito
}
finally {
    Pop-Location
}
