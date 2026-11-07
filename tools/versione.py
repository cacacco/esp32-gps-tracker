# Scrive dentro il firmware da dove viene, come FW_COMMIT: il tag se c'e',
# altrimenti il commit.
#
# Gira prima della compilazione (extra_scripts in platformio.ini). Se git non
# c'e', o la cartella non e' un repository, non blocca niente: il firmware si
# compila lo stesso e src/version.h mette "senza git" al suo posto.
import subprocess

Import("env")   # noqa: F821  (lo passa PlatformIO)

def commit():
    try:
        # describe da' il nome del tag quando siamo esattamente su un tag
        # (v1.0.0), altrimenti "v1.0.0-7-gabc1234", altrimenti il codice del
        # commit: cosi' un firmware rilasciato si presenta col suo numero di
        # versione invece che con un esadecimale.
        testa = subprocess.check_output(
            ["git", "describe", "--tags", "--always"],
            stderr=subprocess.DEVNULL, timeout=10).decode().strip()
        if not testa:
            return None
        # un "+" avvisa che ci sono modifiche non ancora committate
        sporco = subprocess.check_output(
            ["git", "status", "--porcelain"],
            stderr=subprocess.DEVNULL, timeout=10).decode().strip()
        return testa + ("+" if sporco else "")
    except Exception:
        return None

c = commit()
if c:
    env.Append(CPPDEFINES=[("FW_COMMIT", '\\"' + c + '\\"')])   # noqa: F821
    print("[versione] firmware marcato come " + c)
else:
    print("[versione] git non disponibile: il firmware restera' senza commit")
