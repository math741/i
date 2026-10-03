"""Hardware DNA e compilação: o que a MLG sabe sobre o silício, medido agora."""
import concurrent.futures
import ctypes
import glob
import hashlib
import os
import platform
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, ".build")
CC = os.environ.get("CC", "cc")


# --------------------------------------------------------------------------- compilação

def compile_so(sources, out, flags):
    cmd = [CC] + flags + ["-shared", "-fPIC", "-o", out] + sources + ["-lpthread", "-lm"]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    return r.returncode == 0, r.stderr


def detect_flags():
    """Descobre o conjunto de flags mais agressivo que este compilador aceita aqui."""
    os.makedirs(BUILD, exist_ok=True)
    src = os.path.join(BUILD, "flagtest.c")
    with open(src, "w") as f:
        f.write("float f(float*a,int n){float s=0;for(int i=0;i<n;i++)s+=a[i]*a[i];return s;}\n")
    for flags in (["-O3", "-march=native"], ["-O3", "-mcpu=native", "-mfpu=auto"],
                  ["-O3", "-mcpu=native"], ["-O3"], ["-O2"]):
        ok, _ = compile_so([src], os.path.join(BUILD, "flagtest.so"), flags)
        if ok:
            return flags
    sys.exit("nenhum compilador C funcional encontrado (defina CC=...)")


def isa_variants(flags):
    """Escolhas de ISA que entram no espaço de busca (e no probe do teto)."""
    variants = {"default": []}
    if "avx512f" in cpu_flags():
        extra = ["-mprefer-vector-width=512"]
        ok, _ = compile_so([os.path.join(BUILD, "flagtest.c")], os.path.join(BUILD, "flagtest.so"), flags + extra)
        if ok:
            variants["vw512"] = extra
    return variants


def compiler_version():
    r = subprocess.run([CC, "--version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    return (r.stdout.splitlines() or ["?"])[0]


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


FMA_RE = re.compile(r"\b(v?fn?m(add|sub)\d*[a-z]*|fmla|fmls|vfma|vfms)\b")


def count_fma(so):
    """Conta instruções FMA no binário (None se não houver objdump)."""
    try:
        r = subprocess.run(["objdump", "-d", so], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           universal_newlines=True)
    except OSError:
        return None
    return len(FMA_RE.findall(r.stdout)) if r.returncode == 0 else None


# --------------------------------------------------------------------------- DNA do hardware

def read_first(path, default=None):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return default


def cpu_model():
    info = read_first("/proc/cpuinfo", "")
    for key in ("model name", "Model", "Hardware", "cpu model"):
        for line in info.splitlines():
            if line.split(":")[0].strip() == key:
                return line.split(":", 1)[1].strip()
    return platform.processor() or platform.machine()


def cpu_flags():
    for line in (read_first("/proc/cpuinfo", "") or "").splitlines():
        if line.split(":")[0].strip() in ("flags", "Features"):
            return set(line.split(":", 1)[1].split())
    return set()


def temperature():
    temps = []
    for p in glob.glob("/sys/class/thermal/thermal_zone*/temp"):
        v = read_first(p)
        if v and v.strip().lstrip("-").isdigit():
            temps.append(int(v) / 1000.0)
    return max(temps) if temps else None


def meminfo_mb(key):
    for line in (read_first("/proc/meminfo", "") or "").splitlines():
        if line.startswith(key + ":"):
            return int(line.split()[1]) // 1024
    return None


def last_level_cache():
    """(nível, bytes) do maior cache visto pela cpu0."""
    best = (0, 0)
    for d in glob.glob("/sys/devices/system/cpu/cpu0/cache/index*"):
        lvl, size = read_first(d + "/level"), read_first(d + "/size")
        if not lvl or not size:
            continue
        size = size.strip()
        mult = {"K": 1024, "M": 1024 ** 2, "G": 1024 ** 3}.get(size[-1], 1)
        nbytes = int(size.rstrip("KMG")) * mult
        if (int(lvl), nbytes) > best:
            best = (int(lvl), nbytes)
    return best


def calibrate(fn, *args, target=0.15, start=20000):
    """Aumenta iters até a medição durar `target` s; devolve o melhor de 3."""
    iters = start
    while True:
        t0 = time.perf_counter()
        fn(iters, *args)
        if time.perf_counter() - t0 > target:
            break
        iters *= 4
    return [fn(iters, *args) for _ in range(3)]


def probe_hardware(probe, flags, variants, cpus, args):
    for name, res, argt in (("mlg_bandwidth", ctypes.c_double, [ctypes.c_long, ctypes.c_int, ctypes.c_int]),
                            ("mlg_freq", ctypes.c_double, [ctypes.c_long, ctypes.c_int])):
        getattr(probe, name).restype = res
        getattr(probe, name).argtypes = argt
    dna = {"temp_c_before": temperature()}

    # Teto de cálculo: o próprio probe é uma busca (acumuladores x largura de vetor).
    jobs = [(acc, vn) for acc in (32, 64, 128, 256) for vn in variants]

    def build(job):
        acc, vn = job
        so = os.path.join(BUILD, "peak_%d_%s.so" % (acc, vn))
        ok, err = compile_so([os.path.join(HERE, "peak.c")], so,
                             flags + variants[vn] + ["-ffast-math", "-DACC=%d" % acc])
        return job, so if ok else None

    with concurrent.futures.ThreadPoolExecutor(max_workers=cpus) as ex:
        built = dict(ex.map(build, jobs))
    peaks = []
    for (acc, vn), so in sorted(built.items()):
        if not so:
            continue
        fn = ctypes.CDLL(so).mlg_peak_flops
        fn.restype = ctypes.c_double
        fn.argtypes = [ctypes.c_long, ctypes.c_int]
        s1 = calibrate(fn, 1, start=200000 // acc)
        sn = calibrate(fn, cpus, start=200000 // acc)
        peaks.append(dict(acc=acc, isa=vn, samples_1t=s1, samples_all=sn, fma_instr=count_fma(so)))
    dna["peak_variants"] = peaks
    dna["peak_1t"] = max(max(p["samples_1t"]) for p in peaks)
    dna["peak_all"] = max(max(p["samples_all"]) for p in peaks)
    dna["peak_from"] = max(peaks, key=lambda p: max(p["samples_all"]))
    dna["peak_from"] = {"acc": dna["peak_from"]["acc"], "isa": dna["peak_from"]["isa"]}

    dna["freq_1t_hz"] = max(calibrate(probe.mlg_freq, 1, start=100000))
    dna["freq_all_hz"] = max(calibrate(probe.mlg_freq, cpus, start=100000))

    # Banda: conjunto de trabalho >= 4x o último cache, senão mede cache e não DRAM.
    lvl, llc = last_level_cache()
    avail = (meminfo_mb("MemAvailable") or 512) * 1024 ** 2
    ws = args.bw_mb * 1024 ** 2 if args.bw_mb else min(max(4 * llc, 256 * 1024 ** 2), avail // 3)
    n = int(ws / 24)
    reps = max(1, int(1.5e9 / ws))
    dna.update(llc_level=lvl, llc_bytes=llc, bw_working_set=ws,
               bw_dram_ws_over_llc=(ws / llc) if llc else None,
               bw_1t=probe.mlg_bandwidth(n, 1, reps), bw_all=probe.mlg_bandwidth(n, cpus, reps),
               bw_convention="STREAM triad, 24 B/iter (sem write-allocate: tráfego real ~32 B/iter)")
    if llc:
        nc = int(llc / 2 / 24)
        dna["bw_llc_all"] = probe.mlg_bandwidth(nc, cpus, max(1, int(3e9 / (llc / 2))))
    dna["temp_c_after"] = temperature()
    return dna


def fp_env(probe):
    out = (ctypes.c_int * 3)()
    probe.mlg_fp_env(out)
    return {"rounding": {0: "nearest", 1: "down", 2: "up", 3: "zero"}.get(out[0], "?"),
            "ftz": bool(out[1]), "daz": bool(out[2])}


