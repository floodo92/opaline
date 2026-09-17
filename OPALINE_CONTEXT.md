# OPALINE — contexto del proyecto (regenerado 2026-09-17)

Polisher vocal de un knob, serie free de Flodo's Audio Labs. VST3/Windows, JUCE nativo.

## Estado actual (build instalada)
- Build Release `Opaline_VST3` con **UI rediseñada compacta cuadrada** tipo one-knob free.
- Instalada en `C:\Program Files\Common Files\VST3\Opaline.vst3`.
- Hash SHA-256: `DBC2756EA58868A88C328BC33E57590FD50FC3586D1A48BB202D36E5CDBB5A51` (7812608 bytes).
- Validado con `pluginval --validate-in-process --strictness-level 10` → **SUCCESS**.
- Selftest de consola (`Build_Selftest\Release\DspSelfTest.exe`) → **PASS** (cadena audible, exit 0).

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
- Nota: el indicador BRIGHTNESS del selftest baja ~5 dB a Shine 100% porque ya no se pliegan armónicos fantasma sobre >4 kHz — es el oversampling trabajando, no pérdida de brillo real.

## Reglas que se respetaron
- Cero allocs en el hilo de audio (oversampling pre-dimensionado; los `Coefficients` IIR siguen la histéresis `changed()`).
- Biquads de filtros = RBJ manual (`EqStage::makePeak/makeHighShelf`) porque JUCE 8.0.14 (`makePeakFilter/makeHighShelf`) genera coeficientes degenerados a gain 0 que silencian (bug OPALINE).
- SmoothedValue para parámetros; `reset()` de cadenas en transición de bypass.

## Workflow de validación
1. `cmake --build Build --config Release --target Opaline_VST3`
2. `Build_Selftest\Release\DspSelfTest.exe` (regenerar si cambia DSP)
3. `pluginval --validate-in-process --strictness-level 10 <Opaline.vst3>` (exe en `G:\...\Windows\Zenith\pluginval.exe`)
4. Copiar `Build\Opaline_artefacts\Release\VST3\Opaline.vst3` a `C:\Program Files\Common Files\VST3\` y comparar hash (cuidar DAW abiertos).

## Repo git
Repo local en esta carpeta (`Opaline/`), creado con `git init` + `.gitignore` (excluye `Build*`, `opaline_preview.png`, artefactos). Commit inicial del estado actual.

## Regla del triángulo espectral (Paco Rincón)
Protagonistas 500–1000 Hz = B; graves ~200 = B−5; agudos = B−5; sibilancias = B−5..B.
El Learn de OPALINE lo implementa con `airTargetDb` (agudos → B−5) y `sibExcessDb` (exceso de sibilancia sobre el cuerpo), bucket `body` 500–1000 Hz.