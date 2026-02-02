"""
Version: 1.3+
Prompt: PlatformIO build.py – TypeScript-Frontend (npm run build) bauen
und tsc/dist_packed/main.js nach data/www/worker/worker.js kopieren (Windows-kompatibel).
Zusätzlich: icon.ico nach data/www/ (neben index.html) kopieren und mit gzipen.
"""

import gzip
import os
import shutil
from SCons.Script import Import

# PlatformIO-Umgebung importieren
Import("env")


def gzip_file(src: str) -> str:
    dst = src + ".gz"
    with open(src, "rb") as f_in, gzip.open(dst, "wb", compresslevel=9) as f_out:
        shutil.copyfileobj(f_in, f_out)
    print(f"   gz: {dst}")
    return dst


def remove_stale_gz_files(www_root: str) -> None:
    # Alte .gz löschen, damit keine verwaisten Dateien rumliegen
    for root, _, files in os.walk(www_root):
        for name in files:
            if name.endswith(".gz"):
                try:
                    os.remove(os.path.join(root, name))
                except OSError:
                    pass


def first_existing(paths) -> str | None:
    for p in paths:
        if p and os.path.isfile(p):
            return p
    return None


def ensure_icon(project_root: str, tsc_dir: str, www_root: str) -> None:
    """
    Kopiert icon.ico nach data/www/icon.ico (neben index.html),
    falls irgendwo vorhanden.
    """
    dst_icon = os.path.join(www_root, "icon.ico")

    # Typische Orte (je nachdem wie dein Frontend gebaut wird)
    candidates = [
        os.path.join(tsc_dir, "dist_packed", "icon.ico"),
        os.path.join(tsc_dir, "dist_packed", "assets", "icon.ico"),
        os.path.join(tsc_dir, "dist", "icon.ico"),
        os.path.join(tsc_dir, "public", "icon.ico"),
        os.path.join(tsc_dir, "src", "icon.ico"),
        os.path.join(project_root, "icon.ico"),
        os.path.join(project_root, "data", "www", "icon.ico"),
    ]

    src_icon = first_existing(candidates)
    if not src_icon:
        print("WARNUNG: Keine icon.ico gefunden (weder tsc/* noch Projektwurzel).")
        return

    # Wenn Quelle bereits Ziel ist: nichts tun
    if os.path.abspath(src_icon) == os.path.abspath(dst_icon):
        print(f"-> icon.ico vorhanden: {dst_icon}")
        return

    os.makedirs(www_root, exist_ok=True)
    shutil.copy2(src_icon, dst_icon)
    print(f"-> icon.ico kopiert: {src_icon} -> {dst_icon}")


def main():
    # Projektwurzel
    project_root = os.path.abspath(os.getcwd())
    tsc_dir = os.path.join(project_root, "tsc")

    # Source aus TypeScript build output
    src_js = os.path.join(tsc_dir, "dist_packed", "main.js")

    # Ziel: data/www/worker/worker.js
    www_root = os.path.join(project_root, "data", "www")
    worker_dir = os.path.join(www_root, "worker")
    dst_js = os.path.join(worker_dir, "worker.js")

    print("== Mural build.py: TypeScript-Frontend bauen, worker.js + icon.ico aktualisieren, gzip ==")

    # 1) TypeScript-Frontend mit npm bauen
    if not os.path.isdir(tsc_dir):
        print(f"FEHLER: Verzeichnis 'tsc' nicht gefunden: {tsc_dir}")
        return

    old_cwd = os.getcwd()
    try:
        os.chdir(tsc_dir)
        print("-> npm run build")
        env.Execute("npm run build")
    finally:
        os.chdir(old_cwd)

    # 2) Prüfen, ob main.js existiert
    if not os.path.exists(src_js):
        print(f"FEHLER: {src_js} wurde nicht erzeugt. npm-Build fehlgeschlagen?")
        return

    # 3) Zielordner für worker.js anlegen
    os.makedirs(worker_dir, exist_ok=True)

    # 4) Alten Inhalt im worker-Ordner löschen (NUR dort)
    for name in os.listdir(worker_dir):
        path = os.path.join(worker_dir, name)
        if os.path.isfile(path):
            try:
                os.remove(path)
            except OSError:
                pass

    # 5) main.js -> worker.js kopieren
    shutil.copy2(src_js, dst_js)
    print(f"-> Worker aktualisiert: {dst_js}")

    # 6) icon.ico nach data/www kopieren (neben index.html)
    ensure_icon(project_root, tsc_dir, www_root)

    # 7) Alte .gz entfernen (sonst bleiben Leichen liegen)
    remove_stale_gz_files(www_root)

    # 8) Alle Dateien in data/www gzippen (inkl. icon.ico)
    print("-> gzip data/www")
    for root, _, files in os.walk(www_root):
        for name in files:
            if name.endswith(".gz"):
                continue
            path = os.path.join(root, name)
            gzip_file(path)


# Einstiegspunkt
if __name__ == "__main__":
    main()
else:
    # Wenn von PlatformIO importiert wird, auch ausführen
    main()






