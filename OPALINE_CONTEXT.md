# OPALINE — contexto del proyecto (regenerado 2026-09-18 · sesión 2026-09-19)

Polisher vocal de un knob, serie free de Flodo's Audio Labs. VST3/Windows, JUCE nativo.

**Versión: 1.0.1** (bump 2026-10-08) — única fuente: `CMakeLists.txt` →
`project(Opaline VERSION 1.0.1)`; de ahí sale `JucePlugin_VersionString`
(instal ping, UI, host). Anterior: 1.0.0.

**Gotcha del bump**: el `VERSIONINFO` de Windows sale de
`Build_Snapshot/Opaline_artefacts/JuceLibraryCode/Opaline_resources.rc`, que
JUCE genera con `add_custom_command` **sin DEPENDS sobre `Info.txt`** → una vez
creado nunca se regenera. Si al cambiar `project(... VERSION x)` el host sigue
mostrando la versión vieja, **borrar ese `.rc`** y reconstruir (`Info.txt` sí
se actualiza solo).

---

# SESIÓN 2026-09-20 — bias 0,10 + air 2,0 + auto-trim MEASURE + cursor + descripciones

## Qué se hizo (todo instalado y validado, hash `FE23CFF8AC2FA274A25F0E6BB78BF62D5B02B1411D41AF632C2FEE371BC6A0D0`)
1. **bias exciter 0,06 → 0,10** (`VoiceChain.cpp`, decision pendiente de la sesión
   19-sep resuelta): aliasing peor caso **-59,9 dB @ 20,1 kHz** (antes -64,3 a 0,06);
   a 20,1 kHz = inaudible. Gauntlet PASS con 1 warn (umbral -60 por 0,1 dB).
2. **Test CLICK_HUNT nuevo en tools/Gauntlet** (16 tests ahora): detector de
   discontinuidades (delta vs RMS local de la derivada excluyendo al candidato),
   4 escenarios (steady Shine100 / bypass toggle cada 64 ms / knob-jerk 25 % /
   silencio). Opaline: **0 clics** a 48k/256, 44k1/64, 96k/1024. El usuario
   "oía a veces clics" — no provienen del plugin (posible MEASURE o host; ver abajo).
3. **BODY/AIR airBoost 1,2 → 2,0 dB** (pedido del usuario: el knob parecía muerto
   en Bertom). Swing 1,0..3,0 dB a Shine 100. Lowmid intacto (3 dB).
4. **Auto-trim del MEASURE** (nuevo): el Learn mide RMS de los frames con voz
   (gate body, ≥8 frames) → `LearnProfile.autoTrimDb = clamp(-18 - rmsDb, -15, +18)`.
   El processor aplica ganancia a la entrada del wet path y su INVERSA exacta
   a la salida (por bloque, rampa 300 ms): loudness neta idéntica, la cadena solo
   cambia de punto de operación. Persiste en el estado (`autoTrimDb`). Invisible
   por pedido. **Cubierta de test parcial**: MEASURE no es parámetro → Gauntlet
   no lo ejercita; validar de oído (toma débil a -30 dBFS + MEASURE → el comp
   debería empezar a trabajar sin salto de volumen).
5. **Cursor de knobs**: `UpDownResizeCursor` → `NormalCursor` (`Knob.cpp:52`).
6. **Descripciones** tienda + nerds, ES/EN: `Opaline_descripciones.txt`.

## Sesión 2026-09-20 (histórica, resumen corto)
Gauntlet creado (15 tests, hostea el .vst3 real); fix de aliasing: TapeStage a
4× oversampling (tanh corría a rate base, 3er armónico de 15 kHz plegaba a
900 Hz a -28 dB); bias 0,16→0,06; latencia 3→10 muestras. Ver git para detalle.

## Tarde 2026-09-20 — GATE en la free también (instalado 718157..26CE7)
Portado el patrón del Pro (decisión del usuario: su flujo real de la free es
"primero calibrar, después girar el knob"): instancia virgen → GateOverlay
solo con MEASURE grande + promesa "Press MEASURE during the strongest part of
the vocal" + LISTENING % + escape CONTINUE WITHOUT CALIBRATION tras fallo.
Audio nunca bloqueado; recall medido nunca gatea; LAF de botones con fuente
escalable (h*0.30); footer sin medir = "PRESS MEASURE TO CALIBRATE" ámbar.
Gauntlet 15+1 WARN, pluginval 10 SUCCESS. Cambios de esta tarde SIN commit
(política del repo: solo commit a pedido del usuario).

## Decisiones resueltas
- bias = 0,10 (carácter > suelo de aliasing; el warn de -59,9 dB se acepta).
- BODY/AIR: lado aire subido a 2,0 dB; el usuario confirmó que el Learn y el
  Shine recomendado funcionan bien.
- MEASURE no toca el Shine sin permiso: **SÍ lo configura** (59 % para la voz
  del usuario, aprobado: "el learn anda perfecto").

## Git (NO commiteado, a propósito)
Cambios sin commit de las sesiones 19 y 20-sep en Opaline/: bias 0,10, air 2,0,
auto-trim (LearnEngine.{h,cpp} + PluginProcessor.{h,cpp}), cursor, CLICK_HUNT
está en tools/Gauntlet (fuera de este repo), descripciones. Último commit `27209d6`.

## En pausa (no tocar)
Saneado NUMERIC_SANITIZE en Cascade/Signoff/Zenith/AutoClassic, desborde de Wake.

## NUEVO: Opaline Pro (2026-09-20, sesión nocturna)
Fork completo en `G:\...\Windows\Opaline Pro\` (repo aparte, commit `bdd0c51`):
misma cadena + 8 trims por etapa (12 params), UI horizontal 960×400 tipo rack,
Gauntlet 15+1 WARN, pluginval SUCCESS, instalado como "Opaline Pro.vst3"
(hash F3E257…DB1E9C). Contexto propio: `Opaline Pro\OPALINE_PRO_CONTEXT.md`.

---

# SESIÓN 2026-09-19 — Gauntlet + fix de aliasing (CONTINUAR AQUÍ)

## Qué se hizo
1. **Harness `tools/Gauntlet`** (nuevo, fuera de `Opaline/`): app de consola JUCE que hostea el
   `.vst3` real y corre **15 tests extremos** (NaN/Inf, DC, gain staging, tortura de automatización,
   matrices SR/bloque, roundtrip de estado, create/destroy, PERF, aliasing…). Wrapper
   `tools/Gauntlet/run_gauntlet.ps1` con catálogo. Skill nuevo `.opencode/skills/white-sea-gauntlet`.
2. **Bug real de aliasing encontrado y corregido en Opaline** (ver detalle abajo).
3. Test `ALIASING_FLOOR` reescrito: antes medía un solo tono (8003 Hz) y **solo a defaults**, donde
   `Shine=0` apaga la saturación → daba un falso "-92,3 dB limpio". Ahora barre **12 kHz y 15 kHz**
   × 9 configs (default, min, max, cada param a max, 3 aleatorias), **forzando `bypass=0`** (si no,
   "todos a max" dejaba el bypass en 1 y el barrido no probaba nada), y reporta el peor caso.

## El bug
- Síntoma (confirmado por el usuario en SPAN antes del tool): **alias audible a Shine alto**.
- Medido: **-28,1 dB @ 900 Hz** con Shine al 100 % y tono de 15 kHz (a defaults seguía en -92,3 dB).
- Causa: 900 Hz = 45 kHz − 44,1 kHz, o sea el **3er armónico de 15 kHz plegado**. No bastaba el
  oversampling: **el `tanh` del `TapeStage` corría a rate base** (no el exciter: subir el exciter a
  4× dejó el alias *idéntico*, lo que lo descartó; aislado con `chain.process(buffer, ~kStageTape)`).
- Regla: con `f = 15 kHz` y armónicos hasta el 7º hace falta **Nyquist ≥ 7·15 kHz** → **4×** (176,4 kHz).

## Fix aplicado
- `VoiceChain.h`: `HarmonicExciter::osFactor = 2` y **`TapeStage::osFactor = 2`** (2² = **4×**);
  `TapeStage` gana `os` + `getLatencySamples()`; **`VoiceChain::getLatencySamples()` ahora SUMA**
  la del exciter y la del tape (en serie), y `TapeStage::reset()` resetea su `os`.
- `VoiceChain.cpp`: exciter a 4× (prep y `osSpec` a `sr*4`, HP 3k a `sr*4`); **tape reescrito a 4×**
  (pre/de-énfasis, `tanh`, DC blocker y envolvente 5/120 ms ahora a `sr*4`; mezcla dry/wet dentro del
  dominio sobremuestreado, igual que el exciter).
- `VoiceChain.cpp:497`: `bias` del exciter **0,16 → 0,06**. Su 2º armónico (12 kHz → 24 kHz) caía en
  la banda de transición del decimador y sobrevivía (~-55 dB) **tanto con IIR polyphase como con FIR
  equiripple** (el FIR además disparaba la latencia a 43 muestras → descartado).

| | antes | después |
|---|---|---|
| Aliasing (peor caso, 12k/15k, Shine 100) | **-28,1 dB @ 900 Hz** | **-64,3 dB @ 20,1 kHz** |
| Latencia | 3 muestras | **10 muestras** (0,21 ms, el DAW compensa) |
| PERF_BUDGET | 8,0 µs/bloque | **8,6 µs/bloque** (0,3 % de un core) |

## Validación (todo verde)
- Gauntlet **15/15 PASS**, 0 warn, 0 fail, 0 skip (`tools/Gauntlet/report_Opaline.txt`).
- `pluginval --validate-in-process --strictness-level 10` → **SUCCESS**.
- `DspSelfTest.exe` → **PASS** (STEREO-ISOLATION, sin NaN, cadena audible).
- Hash instalado **`2FB66AE0D0B0547CBF96B8D45B0B56D285AEB947A09978ACFFED8631DBF8C4E9`**.
- **Nota**: a `Shine=0` la cadena no corre exciter ni tape → el sonido a defaults está **intacto**
  respecto a la build anterior.

## Decisión PENDIENTE para mañana (es la única abierta)
- **Carácter vs. suelo de aliasing.** El usuario escuchó la build: *"no se ve nada de alias ni en 12
  ni en 15, lo escucho bastante bien"*. Estado actual `bias=0,06` = un pelín de alias **inaudible**
  (-64,3 dB a 20,1 kHz, por encima del oído) conservando ~37 % de los armónicos pares.
- Lever único: `bias` en `VoiceChain.cpp:497` → `0,10` ≈ **-60 dB** (más carácter par, al límite),
  `0,06` = actual, `0,00` = **-77 dB** sin pares. Si el usuario quiere más carácter: **medir 0,10 y
  hacer escucha A/B**. No tocar sin su oído.

## Cómo retomar (comandos exactos)
```powershell
# Gauntlet (recompilar si se toca el harness)
cmake --build tools\Gauntlet\Build --config Release
# Opaline: solo aliasing (rápido) o batería completa
& "tools\Gauntlet\Build\Gauntlet_artefacts\Release\Gauntlet.exe" --only ALIASING_FLOOR "C:\Program Files\Common Files\VST3\Opaline.vst3"
& "tools\Gauntlet\Build\Gauntlet_artefacts\Release\Gauntlet.exe" --out "tools\Gauntlet\report_Opaline.txt" "C:\Program Files\Common Files\VST3\Opaline.vst3"
# Build + install de Opaline
cmake --build Opaline\Build --config Release --target Opaline_VST3
Copy-Item -LiteralPath "Opaline\Build\Opaline_artefacts\Release\VST3\Opaline.vst3\Contents\x86_64-win\Opaline.vst3" -Destination "C:\Program Files\Common Files\VST3\Opaline.vst3\Contents\x86_64-win\Opaline.vst3" -Force
```

## Estado de git (NO commiteado)
`Opaline/` es un repo git aparte. Hay cambios sin commitear de esta sesión **y** de la anterior (UI
de knobs, snapshots, selftest): `CMakeLists.txt`, `Source/DSP/VoiceChain.{h,cpp}`,
`Source/PluginEditor.{h,cpp}`, `Source/PluginProcessor.cpp`, `Source/UI/{Knob,LedMeter}.{h,cpp}`,
`tests/snapshots/stories/Opaline_stories.cpp`, `tools/dsp_selftest/main.cpp`, `assets/fal_mark.svg`
(sin trackear). Último commit `27209d6`. **Nada commiteado a propósito** (el usuario no lo pidió).

## Lo demás sigue en pausa (no tocar)
Saneado de entrada en Cascade/Signoff/Zenith/AutoClassic (`NUMERIC_SANITIZE`), desborde de Wake
(196 dBFS + PERF 109 %). El usuario dijo explícitamente que el foco es Opaline.

---

## Estado actual (build instalada)
- Build Release `Opaline_VST3` con **UI rediseñada compacta cuadrada** tipo one-knob free.
- Instalada en `C:\Program Files\Common Files\VST3\Opaline.vst3`.
- **2026-09-19**: latencia **10 muestras** (4× oversampling en exciter + tape, ver sesión arriba);
  Gauntlet 15/15 PASS; hash `2FB66AE0D0B0547CBF96B8D45B0B56D285AEB947A09978ACFFED8631DBF8C4E9`.
- Hash anterior (18-sep v3, sin el fix de aliasing): `1652C2314D79DB6F2D076EDD728478FECF1068B6468FA73FC322C73B99962379`. **Radio-ready 18-sep v3**: cascada óptica (A leveler 10/250 + B picos 5/150) + presence fijo 4k (+1.5 dB·Shine) + tape glue final A800 vocal (pre/de 3.5k ±2 dB, tanh drive 1.6, comp common-mode 5/120 hasta ~3.5 dB, DC blocker 20 Hz). Base aprobada 18-sep v2 intacta (exciter 3.2/0.55, presence 2.0+0.5·air, air 1.2, óptico thr 0.24+0.10/ratio 1+2.0/knee 6 dB, softclip −1.4 dBFS).
- Validado con `pluginval --validate-in-process --strictness-level 10` → **SUCCESS** (bloques 64–1024, 48k/96k).
- Selftest de consola (`Build_Selftest\Release\DspSelfTest.exe`) → **PASS** (cadena audible, exit 0). BRIGHTNESS 100% → −0.79 dB (señal sintética = 100% sibilancia, el de-esser domea: correcto); sin NaN.

## Bypass mantiene la latencia (§10.14 del skill, 2026-09-17)
`OpalineAudioProcessor` incorpora un **delay feed-forward** del tamaño de `chain.getLatencySamples()`:
- El delay **siempre se alimenta con la entrada** (contenido fresco); solo se LEE cuando `bypassed`, así la conmutación wet↔bypass es continua en el tiempo (sin salto de las muestras pendientes).
- Priming a ceros y limpieza en cada `prepareToPlay` (no se acumula si el host lo llama varias veces).
- El BYPASS interno ahora respeta la latencia reportada al DAW (mismo timing que la cadena húmeda).

## Canvas de diseño
- `Source/UI/Brand.h`: `canvasW=400`, `canvasH=460` (antes 1000×560). La UI escala como unidad y queda **cuadrada/chica**, estilo "one knob" de Waves con todos los controles.
- `Frame::resized` dispone: header (44) con pills **BYPASS** y **MEASURE** → meters IN|OUT (52) → hero **SHINE** (size 140) → knobs **BODY/AIR** + **OUTPUT** (size 84) → status.
- Los 3 knobs muestran **label** de forma nativa (`setShowLabel(true)`); antes el label existía pero no se dibujaba (`Knob.h` `showLabel=false` por defecto y nadie lo activaba) → se veía el knob sin nombre (el bug que notó el usuario).
- **BYPASS** ahora tiene botón en la UI (parámetro existía, faltaba control). Toggle estilo "pill" con LAF local `OpalineButtonsLAF` (measure accent ópalo, bypass verde `cSafe`); sin tocar el sistema de marca global.

## DSP — mejora aplicada
- **CURVA >4k MONÓTONA CON SHINE** (2026-09-18, punto 1): antes el brillo >4k era **anti-monótono** (−4.9 dB a Shine 100%: la compresión apagaba el aire). Rebalanceo de energías:
  - Óptico con **knee C1 6 dB** (patrón del de-esser) y tier más suave: `thr = 0.24+0.10·(1−amount)` (antes 0.18+0.18), `ratio = 1+2.0·amount`, release 120 ms.
  - **Exciter drive 3.2 / mix 0.55** (aprobado), **presence 2.0+0.5·airTargetDb**, **air 1.2·(1.5−body)**.
  - De-esser suavizado: `ceilDb = −(0.35+2.2·amount) − 0.12·sibExcessDb` (antes −(1+4·amount) − 0.25).
  - **NOTA DE OÍDO 18-sep v2**: forzar la curva monótona exigía subir exciter (3.6/0.85) + presence 3.5 + air 2.5 → sonaba **agresivo/descontrolado** (saturación dura en agudos, no la compresión). Restaurado al equilibrio aprobado por oído; el >4k del selftest vuelve a bajar suavemente (−0.95 dB a 100%) porque domina el de-esser (correcto: la señal sintética es 100% sibilancia). La curva "monótona" implementada quedó en el cursor: la compresión suave + knee es lo que controla.
- **Soft-clip de salida** (punto 2): `softClipBlock` tras el paralelo, knee C0-continua `y = x` hasta **−1.4 dBFS** y `start+range·tanh(over/range)` con asíntota 0 dBFS (solo toca picos reales; material normal intacto).
- **Detectores linked estéreo** (punto 4): Óptico y ResonanceSuppressor usan `max(canal0, canal1)` (patrón `jmax(abs(d0), abs(d1))`; si `ch≤1` el segundo puntero es 0 → mono degenera igual). Balance estéreo ya no camina con el GR.
- **Smoothing del GR del supresor** (punto 5): `grSm` `LinearSmoothedValue` ~20 ms (antes `over*10` bruto por bloque → zipper).

## DSP — Radio-ready 18-sep v3 (cascada óptica + presence 4k + tape glue)
Dirección aprobada con el usuario a partir de "¿se puede mejorar a radio ready?" (cascada #1, presence #2 y tape A800 cerrando la cadena; gate y auto-LUFS quedan FUERA por identidad one-knob — Signoff es el finalizer de master).
- **OptoComp ahora es cascada de 2 pases** (lambda `pass`): etapa A "leveler" `thrA = 0.26+0.10·(1−amount)`, `ratA = 1+1.2·amount`, envolvente 10/250 ms; etapa B "picos" `thrB = 0.32+0.10·(1−amount)`, `ratB = 1+1.0·amount`, envolvente 5/150 ms; ambas con knee 6 dB, release ~120 ms, detectores linked (max L/R) y `gainA/gainB` persistentes entre bloques (sin zipper). Retorna `jmax(grA, grB)`.
- **Presence fijo 4 kHz** en `EqStage`: pico adicional `+1.5 dB·amount` (curva dinámica del body 500-1k intacta). Histéresis `changed()` + `reset()` tras reasignar coeficientes.
- **TapeStage (glue final, kStageTape = 1<<6)**: porte del `TapeEmulator.h` de SignoffNative (Studer A800) retuneado a VOZ, tras veredicto "brutalmente honesto" (el modelo de bus haría daño en Opaline):
  - SIN head bump de 70 Hz (el HPF adaptativo ya limpia; evitaría plosivas/rumble).
  - Estilo 30 IPS: `drive 1.6`, pre-énfasis RBJ +2 dB @ 3.5k → `tanh(drive·x)/drive` normalizado (unity a bajo nivel) → de-énfasis −2 dB.
  - Compresión por envolvente **common-mode (L+R)/2** 5/120 ms, `thr 0.40`, hasta ~3.5 dB de laminado (no camina el estéreo).
  - DC blocker primer orden @ 20 Hz tras el moldeo; mezcla `dry + (wet−dry)·amount` (bypass exacto a Shine 0); `kStageTape` y opción de mascara por etapa desde UI.
  - FOCO en el ciclo de vida: `tape.prepare/reset` llamados desde `VoiceChain::prepare/reset`.
- **LEGGO (NaN 18-sep)**: el high-shelf del tape usaba `EqStage::makeHighShelf` (variante A²±1, NO RBJ auténtico) → a 3.5 kHz genera polos fuera del círculo unitario (|p|≈1.038) que **explotan a inf/NaN** después de ~300 muestras (el filtro se vuelve inestable a frecuencias <8k; en el air shelf de 12k nunca se notó). Correto con `rbjHighShelf()` local (RBJ Audio-EQ-Cookbook auténtico, `(A+1)±(A-1)cos(w)`): estable en todo el rango. Diagnóstico por instrumentación: `processSample` con entrada finita daba `-nan(ind)` → coeficientes, no estado.
- **Oversampling 4× en el exciter armónico** (`VoiceChain::HarmonicExciter::osFactor = 2`) con `juce::dsp::Oversampling<float>` half-band polyphase IIR (**era 2× hasta 2026-09-19; el 3er armónico de un tono de 15 kHz a 2× pliega a 900 Hz — ver sesión arriba**):
  - Reduce el aliasing de los armónicos del tanh (banda >3 kHz) → menos "gris" y brillo más limpio a Shine alto.
  - API JUCE 8: `initProcessing` + `processSamplesUp/processSamplesDown` sobre `AudioBlock`. El HP 3 kHz se prepara a la tasa oversampleada (hoy 4× = `sr*4`).
  - `prepare`/`reset` en hilo de control; en audio no hay allocs (los buffers internos se pre-dimensionan en `initProcessing`).
  - La latencia del oversampling se reporta al DAW: `setLatencySamples(chain.getLatencySamples())` en `prepareToPlay` (**ahora suma exciter + tape: 10 muestras**).
  - **NUEVO**: HPF adaptativo en `VoiceChain::EqStage` basado en `LearnProfile.medianPitchHz`:
    - Calculado como `clamp(medianPitchHz * 0.5, 40, 150)` Hz
    - Se mantiene siempre por debajo del fundamental detectado (con margen) para evitar comerse el cuerpo de voces graves
    - Actualizado dinámicamente cuando el Learn produce un perfil válido
    - Evita que voces barítonas pierdan fundamental con HPF fijo en 85 Hz
- Nota: la señal sintética del selftest incluye ráfagas de sibilancia pura que el de-esser domea; por eso BRIGHTNESS al 100% ya no mide "gris" sino el balance neto (ahora positivo/monótono tras el rebalanceo).

## De-Esser rediseñado (techo relativo al cuerpo, 2026-09-17)
**Diseño final** (`VoiceChain::DeEsser`, regla Paco Rincón: la "S" nunca supera el cuerpo 500-1k):
- **Techo dinámico por muestra**: `thr = envBody(500-1k) * 10^(ceilDb/20)`, `ceilDb = -(1+4·amount) - 0.25·sibExcessDb` (más estricto con Shine y con el exceso medido por el Learn). Ratio `1+3·amount` → el nivel TIENDE al techo (compresión musical, no clamp).
- **Referencia de cuerpo = banda 500-1k** (HP 500 + LP 1k por canal, envolvente lenta 10/300 ms linked): no persigue la "S".
- **Detector linked** (max de canales), attack 1 ms, release 120 ms con `gainState` persistente entre bloques (sin zipper).
- **Knee suave 2 dB** C1-continuo; **idle-bypass exacto** (sin voz o sin banda: gain=1 y `d − banda + banda·1 = d`).
- Estado de filtro bp/body **por canal** (estéreo simétrico), centro = `profile.deEssFreq` del Learn con histéresis de reconstrucción.
- **Sin look-ahead ni latencia extra** (el attack de 1 ms cubre el ataque real de una "S", 10-30 ms).

**Lección (regresión que introdujo artefactos y se corrigió el mismo día):** un "look-ahead" mal implementado (buffer circular que leía el PASADO, canal 1 nunca escrito, banda retardada mezclada contra la muestra actual) produjo lisp/swish/filtro peine/imagen desbalanceada. Reglas aprendidas:
1. La banda que se resta debe salir de la MISMA muestra que se procesa.
2. Un look-ahead real exige retrasar el audio (latencia reportada); si no, usar attack rápido.
3. Detectores linked: una sola envolvente avanzada una vez por muestra (max de canales), nunca por canal intercalado.
4. La referencia de cuerpo debe ser banda (500-1k), no solo HP 500 (si no, la "S" contamina el techo).
5. El gain de gain-reduction persiste entre bloques; recomputar por bloque hace zipper.

## Persistencia de estado
`getStateInformation` guarda además `airTargetDb`, `sibExcessDb` y `medianPitchHz` (los necesita el HPF adaptativo y el techo del de-esser tras recargar sesión).

## Reglas que se respetaron
- Cero allocs en el hilo de audio (oversampling pre-dimensionado; los `Coefficients` IIR siguen la histéresis `changed()`).
- Biquads de filtros = RBJ manual (`EqStage::makePeak/makeHighShelf`) porque JUCE 8.0.14 (`makePeakFilter/makeHighShelf`) genera coeficientes degenerados a gain 0 que silencian (bug OPALINE).
- SmoothedValue para parámetros; `reset()` de cadenas en transición de bypass.

## Workflow de validación
1. `cmake --build Build --config Release --target Opaline_VST3`
2. `Build_Selftest\Release\DspSelfTest.exe` (regenerar si cambia DSP)
3. `pluginval --validate-in-process --strictness-level 10 <Opaline.vst3>` (exe en `G:\...\Windows\Zenith\pluginval.exe`)
4. Copiar `Build\Opaline_artefacts\Release\VST3\Opaline.vst3` a `C:\Program Files\Common Files\VST3\` y comparar hash (cuidar DAW abiertos).

## Skills de JUCE/UI clonados (2026-09-17)
Búsqueda de skills "no genéricos" de JUCE y UI. Veredicto: el skill `juce` propio sigue siendo el mejor en DSP (no se reemplaza). Clonados en `C:\Users\MarMaus\.config\opencode\skills\`:
- `juce-ui-fidelity` (mcaserta) → **ACTIVO** (SKILL.md en raíz, lo detecta opencode al reiniciar). Skill de UI en JUCE: loop verificación visual — render headless del Component a PNG + diff perceptual contra mockup (odiff, fallback Pillow) + referencias 110KB (primitivas Graphics, CSS/Figma→JUCE, theme-integration, synth-ui-catalog). Llena el hueco de "verificación visual" del skill `juce`. Requiere `npm i -g odiff-bin` (opcional).
- `juce-agent-toolkit` (danielraffel) → referencia inerte (workflow/ops: build-release, CI, WinSparkle auto-updates, cross-platform port; atado a su JUCE-Plugin-Starter).
- `audio-plugin-dev-skills` (iPlug3, 94★) → referencia inerte (6 sub-skills de validación: pluginval/VST3/editor-host/AU/CLAP/codesign; los de Windows ya los cubre el skill `juce` §11).
- No clonar: `juce-webview-plugin-skill` (UI WebView, no aplica), `plugindev-plugin` (Rust), plugin pago `juce-dev` (generouscorp).

## Repo git
Repo local en esta carpeta (`Opaline/`), creado con `git init` + `.gitignore` (excluye `Build*`, `opaline_preview.png`, artefactos). Commit inicial + commits de DSP (HPF adaptativo + de-esser + bypass con latencia + persistencia y el rebalanceo de curva del 18-sep). Último commit `1727312` (5 puntos DSP 18-sep v2) con working tree limpio. Pendientes del 18-sep v3 (radio-ready) tras validación: commits de cascada/presence/tape.

## Regla del triángulo espectral (Paco Rincón)
Protagonistas 500–1000 Hz = B; graves ~200 = B−5; agudos = B−5; sibilancias = B−5..B.
El Learn de OPALINE lo implementa con `airTargetDb` (agudos → B−5) y `sibExcessDb` (exceso de sibilancia sobre el cuerpo), bucket `body` 500–1000 Hz.