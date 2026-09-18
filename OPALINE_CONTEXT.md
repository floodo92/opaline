# OPALINE — contexto del proyecto (regenerado 2026-09-17)

Polisher vocal de un knob, serie free de Flodo's Audio Labs. VST3/Windows, JUCE nativo.

## Estado actual (build instalada)
- Build Release `Opaline_VST3` con **UI rediseñada compacta cuadrada** tipo one-knob free.
- Instalada en `C:\Program Files\Common Files\VST3\Opaline.vst3`.
- Hash SHA-256: `9A7D601683A96455DEF7013A920E227EE0AE4D894CAA8808B2901097B8339AE4`.
- Validado con `pluginval --validate-in-process --strictness-level 10` → **SUCCESS** (bloques 64–1024, 48k/96k).
- Selftest de consola (`Build_Selftest\Release\DspSelfTest.exe`) → **PASS** (cadena audible, exit 0).

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
- Nota: el indicador BRIGHTNESS del selftest baja monótonamente con Shine (−5.4 dB a 100%) porque la señal sintética es 100% sibilancia en la banda alta y el de-esser la domea (trabaja bien); en voz real el brillo lo ponen exciter/air sobre contenido no sibilante.

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
Repo local en esta carpeta (`Opaline/`), creado con `git init` + `.gitignore` (excluye `Build*`, `opaline_preview.png`, artefactos). Commit inicial del estado actual. **UNCOMMITTED**: Source/DSP/VoiceChain.{h,cpp} (HPF adaptativo + de-esser), Source/PluginProcessor.{h,cpp} (bypass con latencia + persistencia) — el binario instalado ya incluye estos cambios (hash `9A7D6016...`).

## Regla del triángulo espectral (Paco Rincón)
Protagonistas 500–1000 Hz = B; graves ~200 = B−5; agudos = B−5; sibilancias = B−5..B.
El Learn de OPALINE lo implementa con `airTargetDb` (agudos → B−5) y `sibExcessDb` (exceso de sibilancia sobre el cuerpo), bucket `body` 500–1000 Hz.