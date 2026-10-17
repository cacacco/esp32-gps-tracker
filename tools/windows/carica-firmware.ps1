<#
    carica-firmware.ps1 - compila e carica il firmware dell'ESP32 GPS Tracker.

    Si usa con un doppio clic su carica-firmware.bat, che lo lancia senza
    toccare i criteri di esecuzione di PowerShell. Da riga di comando:

        .\carica-firmware.ps1                  compila, carica e apre il log
        .\carica-firmware.ps1 -SoloMonitor     apre solo il log seriale
        .\carica-firmware.ps1 -SoloCompila     compila senza caricare
        .\carica-firmware.ps1 -Pulisci         ricompila tutto da zero
        .\carica-firmware.ps1 -Porta COM7      forza la porta seriale
        .\carica-firmware.ps1 -WiFi            carica via WiFi, senza cavo

    Se PlatformIO non c'e', lo installa (serve Python 3 sul PC).
#>
[CmdletBinding()]
param(
    [string]$Porta,
    [switch]$SoloMonitor,
    [switch]$SoloCompila,
    [switch]$SenzaMonitor,
    [switch]$Pulisci,
    [switch]$WiFi,
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

# Una porta malformata, arrivata per errore da uno script, deve fermarsi qui:
# piu' avanti diventerebbe un messaggio incomprensibile di esptool.
if ($Porta -and $Porta -notmatch '^COM\d+$') {
    Scrivi ""
    Scrivi "Porta '$Porta' non valida: serve un nome tipo COM7." 'Red'
    exit 1
}

# --- menu, se lanciato senza opzioni (tipico del doppio clic) --------------
if (-not ($Auto -or $SoloMonitor -or $SoloCompila -or $SenzaMonitor -or $Pulisci -or $WiFi -or $Porta)) {
    Scrivi ""
    Scrivi "Cosa vuoi fare?"
    Scrivi "  [1] Compila, carica e apri il log seriale   (predefinito)"
    Scrivi "  [2] Solo log seriale"
    Scrivi "  [3] Solo compilazione"
    Scrivi "  [4] Pulisci e ricompila da zero"
    Scrivi "  [5] Carica via WiFi, senza cavo"
    $scelta = Read-Host "Scelta [1]"
    switch ($scelta) {
        '2' { $SoloMonitor = $true }
        '3' { $SoloCompila = $true }
        '4' { $Pulisci     = $true }
        '5' { $WiFi        = $true }
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

    if ($WiFi) {
        Scrivi ""
        Scrivi "Compilo e carico via WiFi (nessun cavo, la scheda dev'essere accesa)..." 'Cyan'
        & $pio run -e ota -t upload
        $esito = $LASTEXITCODE
        if ($esito -ne 0) {
            Scrivi ""
            Scrivi "Caricamento via WiFi non riuscito. Le cause tipiche:" 'Yellow'
            Scrivi "  - la scheda e' spenta, o non e' sulla stessa rete del PC"
            Scrivi "  - il firmware a bordo e' precedente all'OTA: il primo"
            Scrivi "    caricamento va fatto via USB, poi gli altri vanno via WiFi"
            Scrivi "  - il nome gps-tracker.local non si risolve: metti l'indirizzo IP"
            Scrivi "    al posto di upload_port in platformio.ini, sezione [env:ota]"
            Scrivi "  - password sbagliata: deve combaciare con OTA_PASSWORD di config.h"
        } else {
            Scrivi ""
            Scrivi "Caricato. Il log seriale pero' viaggia sul cavo: per vederlo" 'Green'
            Scrivi "collega la USB e usa la voce [2] del menu." 'Green'
        }
        exit $esito
    }

    # Il monitor seriale tiene la porta: e' di gran lunga il motivo piu' comune
    # per cui un caricamento fallisce, e capita proprio a chi sta guardando il
    # log mentre lavora. Meglio dirlo prima che dopo.
    if (-not $SoloCompila) {
        Scrivi ""
        Scrivi "Se hai un log seriale aperto in un'altra finestra, chiudilo adesso:" 'Yellow'
        Scrivi "finche' e' aperto tiene occupata la porta e il caricamento non parte." 'Yellow'
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

    # Fino a tre tentativi: quasi sempre basta chiudere il log rimasto aperto,
    # e ricominciare da capo con tutta la compilazione sarebbe una penitenza.
    $tentativo = 0
    while ($true) {
        $tentativo++
        & $pio @argomenti
        $esito = $LASTEXITCODE
        if ($esito -eq 0) { break }

        Scrivi ""
        Scrivi "Qualcosa non ha funzionato (codice $esito). Le cause tipiche:" 'Yellow'
        Scrivi "  - un altro programma tiene occupata la porta: un log seriale rimasto"
        Scrivi "    aperto in un'altra finestra, u-center, l'IDE di Arduino. E' il caso"
        Scrivi "    piu' frequente, e si risolve chiudendo quella finestra"
        Scrivi "  - la scheda non entra in modalita' caricamento: tieni premuto BOOT"
        Scrivi "    quando compare 'Connecting....' e rilascia appena inizia a scrivere"
        Scrivi "  - porta sbagliata: guarda l'elenco qui sotto e rilancia con -Porta COMx"
        Scrivi "  - cavo USB di sola ricarica, senza i fili dati"
        Scrivi ""
        & $pio device list

        if ($SoloCompila -or $tentativo -ge 3) { break }

        Scrivi ""
        Scrivi "Chiudi l'eventuale finestra del log, poi premi INVIO per riprovare." 'Cyan'
        $risposta = Read-Host "Riprovo? [S/n]"
        if ($risposta -match '^[nN]') { break }
        Scrivi ""
        Scrivi "Riprovo (tentativo $($tentativo + 1) di 3)..." 'Cyan'
    }

    exit $esito
}
finally {
    Pop-Location
}
