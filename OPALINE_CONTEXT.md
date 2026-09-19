# OPALINE — contexto del proyecto (regenerado 2026-09-18)

Polisher vocal de un knob, serie free de Flodo's Audio Labs. VST3/Windows, JUCE nativo.

## Estado actual (build instalada)
- Build Release `Opaline_VST3` con **UI rediseñada compacta cuadrada** tipo one-knob free.
- Instalada en `C:\Program Files\Common Files\VST3\Opaline.vst3`.
- Hash SHA-256: `1652C2314D79DB6F2D076EDD728478FECF1068B6468FA73FC322C73B99962379`. **Radio-ready 18-sep v3**: cascada óptica (A leveler 10/250 + B picos 5/150) + presence fijo 4k (+1.5 dB·Shine) + tape glue final A800 vocal (pre/de 3.5k ±2 dB, tanh drive 1.6, comp common-mode 5/120 hasta ~3.5 dB, DC blocker 20 Hz). Base aprobada 18-sep v2 intacta (exciter 3.2/0.55, presence 2.0+0.5·air, air 1.2, óptico thr 0.24+0.10/ratio 1+2.0/knee 6 dB, softclip −1.4 dBFS).
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
- **Oversampling 2× en el exciter armónico** (`VoiceChain::HarmonicExciter`) con `juce::dsp::Oversampling<float>` half-band polyphase IIR:
  - Reduce el aliasing de los armónicos del tanh (banda >3 kHz) → menos "gris" y brillo más limpio a Shine alto.
  - API JUCE 8: `initProcessing` + `processSamplesUp/processSamplesDown` sobre `AudioBlock`. El HP 3 kHz se prepara a la tasa oversampleada (2×).
  - `prepare`/`reset` en hilo de control; en audio no hay allocs (los buffers internos se pre-dimensionan en `initProcessing`).
  - La latencia del oversampling se reporta al DAW: `setLatencySamples(chain.getLatencySamples())` en `prepareToPlay`.
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