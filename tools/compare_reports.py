#!/usr/bin/env python3
# =============================================================================
#  compare_reports.py — gate de PARIDAD entre dos informes del Gauntlet
#
#  QUE HACE
#    Compara dos informes del Gauntlet (el mismo plugin, dos builds) y decide
#    si el comportamiento cambio. Se usa en DOS direcciones:
#
#      1) NO REGRESION EN WINDOWS (baseline vs post-fix, ambos en Windows):
#           el plugin debe seguir comportandose EXACTAMENTE igual.
#      2) PARIDAD MAC <-> WINDOWS (Windows vs Mac):
#           el plugin debe comportarse igual en las dos plataformas.
#
#  USO
#    python3 compare_reports.py baseline.txt nuevo.txt [--mode baseline|cross]
#                                     [--report-json paridad.json]
#                                     [--allow-warn TEST ...]
#
#    --mode baseline  : paridad estricta (Windows antes vs Windows despues).
#                       Solo difieren el CPU (tolerancia) y los tiempos.
#    --mode cross     : Windows vs Mac. Tolera lo que es propio de la
#                       plataforma (CPU, denormales en ARM, FMA en el ultimo
#                       digito) y exige paridad en todo lo demas.
#
#  SALIDA
#    0 = paridad OK
#    1 = hay diferencias que rompen la paridad
#    2 = error de uso / informe ilegible
#
#  Solo stdlib. Funciona con python3 de macOS y de los runners de Windows.
# =============================================================================
import argparse
import json
import re
import sys

# ---------------------------------------------------------------------------
#  Formato del informe del Gauntlet:
#    "  PASS  TEST_NAME  detalle..."
# ---------------------------------------------------------------------------
LINE_RE = re.compile(r'^\s*(PASS|WARN|FAIL|SKIP)\s+([A-Z][A-Z0-9_]*)\s+(.*)$')

# metricas extraidas del detalle de cada test.
#   (regex, nombre, tipo)   tipo: exact | num (con tolerancia) | info
METRICS = {
    'LOAD_SMOKE': [
        (re.compile(r'in=(\d+)'), 'in', 'exact'),
        (re.compile(r'out=(\d+)'), 'out', 'exact'),
        (re.compile(r'params=(\d+)'), 'params', 'exact'),
        (re.compile(r'latency=(\d+)'), 'latency', 'exact'),
    ],
    'TORTURE_AUTOMATION': [
        (re.compile(r'max\|y\|=(-?[\d.]+)\s*dBFS'), 'max_y_dbfs', 'num'),
    ],
    'EXTREME_SWEEP': [
        (re.compile(r'max\|y\|=(-?[\d.]+)\s*dBFS'), 'max_y_dbfs', 'num'),
    ],
    'DC_OFFSET': [
        (re.compile(r'DC=(-?[\d.]+)\s*dBFS'), 'dc_dbfs', 'num'),
    ],
    'NUMERIC_SANITIZE': [
        (re.compile(r'tras\s+(\d+)\s+bloques'), 'clean_blocks', 'num'),
    ],
    'GAIN_STAGING': [
        (re.compile(r'max\|y\|=(-?[\d.]+)\s*dBFS'), 'max_y_dbfs', 'num'),
    ],
    'IMPULSE_RESPONSE': [
        (re.compile(r'max\|y\|=(-?[\d.]+)\s*dBFS'), 'max_y_dbfs', 'num'),
        (re.compile(r'en\s+([\d.]+)\s*ms'), 'decay_ms', 'num'),
    ],
    'DENORMAL_SILENCE': [
        (re.compile(r'silencio/ruido=([\d.]+)x'), 'ratio', 'info'),
    ],
    'BLOCK_SIZE_MATRIX': [
        (re.compile(r'(\d+)\s+combinaciones'), 'combinations', 'exact'),
    ],
    'CHANNEL_LAYOUTS': [
        (re.compile(r'acepta:\s*(.+)'), 'layouts', 'exact'),
    ],
    'LATENCY_STABILITY': [
        (re.compile(r'latencia=(\d+)\s+muestras'), 'latency', 'exact'),
    ],
    'STATE_ROUNDTRIP': [
        (re.compile(r'(\d+)\s+bytes'), 'state_bytes', 'exact'),
    ],
    'CREATE_DESTROY': [
        (re.compile(r'(\d+)\s+ciclos'), 'cycles', 'exact'),
    ],
    'PERF_BUDGET': [
        (re.compile(r'CPU=([\d.]+)%'), 'cpu_pct', 'info'),
        (re.compile(r'\(([\d.]+)\s*us/bloque\)'), 'us_per_block', 'info'),
    ],
    'ALIASING_FLOOR': [
        (re.compile(r"peor caso '[^']*':\s*(-?[\d.]+)\s*dB"), 'alias_db', 'num'),
        (re.compile(r'pico\s+([\d.]+)\s*Hz'), 'alias_hz', 'num'),
        (re.compile(r'en\s+(\d+)\s+medidas'), 'measures', 'exact'),
    ],
    'CLICK_HUNT': [
        (re.compile(r'(\d+)\s+clics'), 'clicks', 'exact'),
        (re.compile(r'residuo.*?(-?[\d.]+)\s*dBFS'), 'residual_dbfs', 'num'),
    ],
}

# Tolerancias por metrica (absoluta).
TOL = {
    'max_y_dbfs': 0.5,       # dB
    'dc_dbfs': 3.0,          # dB
    'clean_blocks': 2,       # bloques
    'decay_ms': 0.5,         # ms
    'alias_db': 3.0,         # dB
    'alias_hz': 60.0,        # Hz (la imagen de alias debe caer en el MISMO bin)
    'residual_dbfs': 3.0,    # dB
}

# Metricas que solo se informan (no generan FAIL) en modo cross.
INFO_IN_CROSS = {'ratio', 'cpu_pct', 'us_per_block'}

# Metricas no deterministas por construccion (mediciones de ruido): se informan
# en AMBOS modos. Verificado con 3 corridas del mismo binario y el mismo seed.
NONDETERMINISTIC = {'ratio'}

# Tolerancia de CPU en modo baseline (antes vs despues, misma maquina).
CPU_TOL_PCT_BASELINE = 25.0


# ---------------------------------------------------------------------------
def parse_report(path):
    """Devuelve {TEST: {'status':..., 'detail':..., 'metrics':{...}}}"""
    tests = {}
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as fh:
            for raw in fh:
                m = LINE_RE.match(raw.rstrip('\n'))
                if not m:
                    continue
                status, name, detail = m.group(1), m.group(2), m.group(3).strip()
                if name not in METRICS:
                    continue
                metrics = {}
                for rx, key, _kind in METRICS[name]:
                    mm = rx.search(detail)
                    if mm:
                        raw_val = mm.group(1)
                        try:
                            metrics[key] = float(raw_val) if '.' in raw_val else int(raw_val)
                        except ValueError:
                            metrics[key] = raw_val
                tests[name] = {'status': status, 'detail': detail, 'metrics': metrics}
    except OSError as exc:
        print('::error::No se pudo leer %s: %s' % (path, exc))
        return None
    if not tests:
        print('::error::%s no parece un informe del Gauntlet (0 tests parseados)' % path)
        return None
    return tests


def compare(base, other, mode):
    """Devuelve (lineas, n_fail, n_diff)"""
    lines = []
    fails = 0
    diffs = 0

    only_base = sorted(set(base) - set(other))
    only_other = sorted(set(other) - set(base))

    for name in only_base:
        lines.append('  [DIFF] %s: presente en el baseline y AUSENTE en el nuevo' % name)
        fails += 1
    for name in only_other:
        lines.append('  [DIFF] %s: NUEVO test (no estaba en el baseline)' % name)
        diffs += 1

    for name in sorted(set(base) & set(other)):
        a, b = base[name], other[name]

        # 1) veredicto del test
        if a['status'] != b['status']:
            sev = 'FAIL'
            if a['status'] == 'FAIL' and b['status'] == 'SKIP':
                sev = 'DIFF'
            if sev == 'FAIL':
                fails += 1
            else:
                diffs += 1
            lines.append('  [%s] %s: veredicto %s -> %s' % (sev, name, a['status'], b['status']))
            lines.append('         base: %s' % a['detail'])
            lines.append('         nuevo: %s' % b['detail'])

        # 2) metricas
        keys = sorted(set(a['metrics']) | set(b['metrics']))
        for key in keys:
            va = a['metrics'].get(key)
            vb = b['metrics'].get(key)
            if va is None or vb is None:
                lines.append('  [DIFF] %s.%s: presente solo en uno (base=%s nuevo=%s)'
                             % (name, key, va, vb))
                diffs += 1
                continue

            if key in ('cpu_pct', 'us_per_block'):
                if mode == 'baseline':
                    if isinstance(va, (int, float)) and isinstance(vb, (int, float)) and va > 0:
                        drift = abs(vb - va) / va * 100.0
                        if drift > CPU_TOL_PCT_BASELINE:
                            lines.append('  [FAIL] %s.%s: CPU %.1f%% -> %.1f%% (deriva %.0f%% > %.0f%%)'
                                         % (name, key, va, vb, drift, CPU_TOL_PCT_BASELINE))
                            fails += 1
                        else:
                            lines.append('  [OK]   %s.%s: %.1f -> %.1f (deriva %.0f%%, dentro del %.0f%%)'
                                         % (name, key, va, vb, drift, CPU_TOL_PCT_BASELINE))
                else:
                    lines.append('  [INFO] %s.%s: %.1f -> %.1f (el CPU es propio de la plataforma)'
                                 % (name, key, va, vb))
                continue

            # Metricas NO deterministas por construccion: son mediciones de ruido,
            # no un contrato de DSP. Medidas 3 veces sobre el MISMO binario y dan
            # valores distintos (p.ej. 0.92 / 0.93 / 0.96), asi que compararlas
            # como exactas genera falsos FAIL. El gate real de estos tests es su
            # veredicto PASS/FAIL (que SI se compara exactamente mas arriba).
            if key in NONDETERMINISTIC:
                lines.append('  [INFO] %s.%s: %s -> %s (metrica no determinista: el veredicto PASS/FAIL del test es el gate)'
                             % (name, key, va, vb))
                continue

            if key in INFO_IN_CROSS and mode == 'cross':
                lines.append('  [INFO] %s.%s: %s -> %s (informativo en Mac)'
                             % (name, key, va, vb))
                continue

            if isinstance(va, (int, float)) and isinstance(vb, (int, float)):
                tol = TOL.get(key, 0.0)
                diff = abs(float(vb) - float(va))
                if diff > tol:
                    lines.append('  [FAIL] %s.%s: %s -> %s (delta %.4g > tolerancia %.4g)'
                                 % (name, key, va, vb, diff, tol))
                    fails += 1
                else:
                    lines.append('  [OK]   %s.%s: %s -> %s (delta %.4g <= %.4g)'
                                 % (name, key, va, vb, diff, tol))
            else:
                if str(va).strip() != str(vb).strip():
                    lines.append('  [FAIL] %s.%s: "%s" -> "%s" (cambio textual)'
                                 % (name, key, va, vb))
                    fails += 1
                else:
                    lines.append('  [OK]   %s.%s: identico' % (name, key))

    return lines, fails, diffs


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument('baseline')
    ap.add_argument('nuevo')
    ap.add_argument('--mode', choices=['baseline', 'cross'], default='cross')
    ap.add_argument('--report-json', default=None)
    ap.add_argument('--allow-warn', action='append', default=[])
    args = ap.parse_args()

    base = parse_report(args.baseline)
    if base is None:
        return 2
    other = parse_report(args.nuevo)
    if other is None:
        return 2

    lines, fails, diffs = compare(base, other, args.mode)

    print('=' * 72)
    print(' PARIDAD %s  |  baseline: %s  |  nuevo: %s  |  modo: %s'
          % ('Mac <-> Windows' if args.mode == 'cross' else 'Windows antes/despues',
             args.baseline, args.nuevo, args.mode))
    print('=' * 72)
    for ln in lines:
        print(ln)
    print('-' * 72)
    print(' RESULTADO: %d FAIL, %d DIFF' % (fails, diffs))
    print('=' * 72)

    if args.report_json:
        payload = {
            'mode': args.mode,
            'baseline': args.baseline,
            'nuevo': args.nuevo,
            'fail': fails,
            'diff': diffs,
            'lines': lines,
        }
        try:
            with open(args.report_json, 'w', encoding='utf-8') as fh:
                json.dump(payload, fh, indent=2, ensure_ascii=False)
        except OSError as exc:
            print('::warning::No se pudo escribir %s: %s' % (args.report_json, exc))

    if fails:
        print('::error::La paridad NO se cumple: %d diferencias bloqueantes' % fails)
        return 1

    print('::notice::Paridad OK (%d diferencias informativas)' % diffs)
    return 0


if __name__ == '__main__':
    sys.exit(main())
