#!/usr/bin/env python3
"""PiFone: conecta no fone Bluetooth, toca as musicas e obedece aos botoes."""
import glob
import os
import re
import select
import subprocess
import threading
import time

MUSICAS = "/srv/musicas"
CONFS = ["/boot/firmware/pifone.txt", "/boot/pifone.txt"]
MAC_RE = re.compile(r"Device ((?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2})")

want_play = False


def log(*a):
    print(*a, flush=True)


def sh(*cmd, timeout=30):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.stdout
    except Exception:
        return ""


def read_conf():
    conf = {}
    for path in CONFS:
        try:
            with open(path, encoding="utf-8", errors="ignore") as f:
                for line in f:
                    line = line.strip().lstrip("﻿")
                    if line and not line.startswith("#") and "=" in line:
                        k, v = line.split("=", 1)
                        conf[k.strip().upper()] = v.strip()
            break
        except OSError:
            continue
    return conf


# ---------------------------------------------------------------- Bluetooth

def bt(*args, timeout=20):
    return sh("bluetoothctl", *args, timeout=timeout)


def all_devices():
    return [m.upper() for m in MAC_RE.findall(bt("devices"))]


def info(mac):
    return bt("info", mac)


def is_audio(txt):
    low = txt.lower()
    return "0000110b" in low or "icon: audio-" in low


def is_paired(txt):
    return "Paired: yes" in txt


def is_connected(txt):
    return "Connected: yes" in txt


def rssi(txt):
    m = re.search(r"RSSI:.*\((-?\d+)\)", txt) or re.search(r"RSSI:\s*(-?\d+)", txt)
    return int(m.group(1)) if m else -999


def paired_fones(only=None):
    out = []
    for mac in all_devices():
        if only and mac != only:
            continue
        txt = info(mac)
        if is_paired(txt) and (is_audio(txt) or only):
            out.append(mac)
    return out


def connected_fone(only=None):
    for mac in all_devices():
        if only and mac != only:
            continue
        txt = info(mac)
        if is_connected(txt) and (is_audio(txt) or only):
            return mac
    return None


def pair_new(only=None):
    """Procura um fone em modo de pareamento e pareia com o mais proximo."""
    log("Procurando fone em modo de pareamento...")
    bt("--timeout", "12", "scan", "on", timeout=25)
    cands = []
    for mac in all_devices():
        if only and mac != only:
            continue
        txt = info(mac)
        if is_paired(txt):
            continue
        if only or is_audio(txt):
            cands.append((rssi(txt), mac))
    if not cands:
        return
    cands.sort(reverse=True)
    mac = cands[0][1]
    log("Pareando com", mac)
    bt("pair", mac, timeout=40)
    bt("trust", mac)
    bt("connect", mac, timeout=25)


def forget_all():
    for mac in all_devices():
        log("Esquecendo", mac)
        bt("remove", mac)


# ---------------------------------------------------------------- Tocador

def mpc(*args, timeout=30):
    return sh("mpc", *args, timeout=timeout)


def playing():
    return "[playing]" in mpc("status")


def sync_queue():
    """Deixa a fila igual a biblioteca (todas as musicas)."""
    if "Updating DB" in mpc("status"):
        return
    m = re.search(r"Songs:\s*(\d+)", mpc("stats"))
    songs = int(m.group(1)) if m else 0
    qlen = len(mpc("playlist").splitlines())
    if songs != qlen:
        log("Atualizando fila: %d musicas" % songs)
        mpc("clear")
        if songs:
            mpc("add", "/")


def check_marker():
    """Arquivo NOVO-FONE na unidade MUSICAS = esquecer o fone e parear de novo."""
    marks = [p for p in glob.glob(os.path.join(MUSICAS, "*"))
             if os.path.basename(p).upper().startswith("NOVO-FONE")]
    if not marks:
        return False
    sh("mount", "-o", "remount,rw", MUSICAS)
    for p in marks:
        try:
            os.rmdir(p) if os.path.isdir(p) else os.remove(p)
        except OSError as ex:
            log("Nao consegui apagar", p, ex)
    sh("sync")
    sh("mount", "-o", "remount,ro", MUSICAS)
    return True


# ---------------------------------------------------------------- Botoes

def buttons():
    global want_play
    from evdev import InputDevice, ecodes as e, list_devices

    acts = {
        e.KEY_PLAYCD: ("toggle",),
        e.KEY_PAUSECD: ("toggle",),
        e.KEY_PLAYPAUSE: ("toggle",),
        e.KEY_PLAY: ("toggle",),
        e.KEY_STOPCD: ("pause",),
        e.KEY_NEXTSONG: ("next",),
        e.KEY_PREVIOUSSONG: ("prev",),
        e.KEY_FASTFORWARD: ("seek", "+10"),
        e.KEY_REWIND: ("seek", "-10"),
    }
    devs = {}
    while True:
        try:
            for path in list_devices():
                if path in devs:
                    continue
                try:
                    d = InputDevice(path)
                    keys = d.capabilities().get(e.EV_KEY, [])
                    if e.KEY_PLAYCD in keys or e.KEY_NEXTSONG in keys:
                        devs[path] = d
                        log("Botoes:", d.name)
                    else:
                        d.close()
                except OSError:
                    pass
            if not devs:
                time.sleep(2)
                continue
            ready, _, _ = select.select(list(devs.values()), [], [], 2)
            for d in ready:
                try:
                    for ev in d.read():
                        if ev.type == e.EV_KEY and ev.value == 1 and ev.code in acts:
                            want_play = False
                            log("Botao:", *acts[ev.code])
                            mpc(*acts[ev.code])
                except OSError:
                    devs.pop(d.path, None)
                    try:
                        d.close()
                    except Exception:
                        pass
        except Exception as ex:
            log("Erro nos botoes:", ex)
            time.sleep(2)


# ---------------------------------------------------------------- Principal

def main():
    global want_play
    conf = read_conf()
    only = conf.get("FONE_MAC", "").upper() or None
    shuffle = conf.get("ALEATORIO", "nao").lower() in ("sim", "s", "yes", "1")

    sh("rfkill", "unblock", "bluetooth")
    bt("power", "on")
    time.sleep(2)

    # As musicas sao copiadas com o cartao no computador, entao a biblioteca
    # precisa ser relida a cada vez que o Pi liga.
    log("Lendo a pasta de musicas...")
    mpc("--wait", "update", timeout=900)
    sync_queue()
    mpc("repeat", "on")
    mpc("random", "on" if shuffle else "off")
    mpc("volume", "100")

    threading.Thread(target=buttons, daemon=True).start()

    was_connected = False
    while True:
        try:
            if check_marker():
                mpc("pause")
                forget_all()
                was_connected = False

            mac = connected_fone(only)
            if mac:
                if not was_connected:
                    log("Fone conectado:", mac)
                    was_connected = True
                    want_play = True
                    time.sleep(3)  # espera o canal de audio ficar pronto
                    mpc("enable", "1")
                if playing():
                    want_play = False
                elif want_play:
                    mpc("play")
                time.sleep(3)
            else:
                if was_connected:
                    log("Fone desconectado")
                    was_connected = False
                    mpc("pause")
                known = paired_fones(only)
                for m in known:
                    bt("connect", m, timeout=15)
                    if is_connected(info(m)):
                        break
                else:
                    if not known:
                        pair_new(only)
                    time.sleep(3)
        except Exception as ex:
            log("Erro:", ex)
            time.sleep(5)


if __name__ == "__main__":
    main()
