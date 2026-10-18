# Scrive il commit di git dentro il firmware, come FW_COMMIT.
#
# Gira prima della compilazione (extra_scripts in platformio.ini). Se git non
# c'e', o la cartella non e' un repository, non blocca niente: il firmware si
# compila lo stesso e src/version.h mette "senza git" al suo posto.
import subprocess

Import("env")   # noqa: F821  (lo passa PlatformIO)

def commit():
    try:
        testa = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
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
