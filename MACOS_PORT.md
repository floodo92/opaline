# macOS port — Opaline 1.0.1

Auditoría y port a macOS ejecutados con el skill `macos-build-audit` (familia `Windows/`).
**Regla 0: Windows es sagrado.** Ningún fix de este port cambia cómo suena o cómo se comporta el
plugin en Windows; está demostrado con un gate automático (Fase 6), no supuesto.

```
Fase 0  Inventario          : OK    (8/8 hechos determinados)
Fase 0b Baseline Windows     : OK    (tests/baseline/windows/)
Fase 1  Compilación (estática): OK    (0 FAIL, 3 WARN aceptados y documentados)
Fase 2  CI                   : OK*   (*workflow creado y validado sintácticamente; falta el 1er run en Mac)
Fase 3  DSP                  : OK en Windows / PENDIENTE en Mac
Fase 4  UI                   : OK en Windows / PENDIENTE en Mac
Fase 5  Validación runtime   : OK en Windows (pluginval SUCCESS, Gauntlet, self-tests) / PENDIENTE en Mac
Fase 6  Paridad              : baseline 0 FAIL ✅ | cross PENDIENTE (requiere Mac)
Windows sin cambios         : OK    (gate de paridad: 0 FAIL, 0 DIFF)
Veredicto: NO PUBLICABLE TODAVÍA — falta el run en macOS (Fases 2-6 en Mac).
```

---

## 1. Fase 0 — Inventario (los 8 hechos, sin supuestos)

| Dato | Valor | Dónde se determinó |
|------|-------|--------------------|
| Versión de JUCE | **9.0.4** | `C:\JUCE\CMakeLists.txt` → `project(JUCE VERSION 9.0.4)`; CI pinea el tag `9.0.4` (verificado: existe en GitHub) |
| Cómo llega JUCE | `JUCE_PATH` (CMake) → env `JUCE_PATH` → `<repo>/JUCE` → fallback `C:/JUCE` | `CMakeLists.txt:34-48` |
| Formatos | Windows: `VST3 Standalone` · Apple: `VST3 AU Standalone` | `CMakeLists.txt:53-56, 73` |
| Tipo de UI | **nativa JUCE** (sin WebView; `JUCE_WEB_BROWSER=0`) | `CMakeLists.txt:115` |
| Red / licencia | `juce::URL` en `Source/InstallPing.cpp` con `JUCE_USE_CURL=0` → **sin red en ninguna plataforma** | `CMakeLists.txt:116`, `InstallPing.cpp:47-64` |
| Assets | 10 fuentes (`Inter`, `Barlow`, `Mono`) + `fal_mark.svg` vía `juce_add_binary_data`; todas presentes | `CMakeLists.txt:83-96` |
| Paths de estado | `juce::File::getSpecialLocation(userApplicationDataDirectory)` → en Mac `~/Library/Application Support` (portable, sin `%APPDATA%`) | `InstallPing.cpp:14` |
| Tests | `tools/{dsp_selftest,bypass_test,calibrate}` (CMake) + `tools/compare_reports.py` + harness de snapshots. El **Gauntlet vive a nivel `Windows/tools/Gauntlet`**, no está vendorizado | `tools/`, `tests/` |

## 2. Fase 0b — Baseline de Windows (el contrato congelado)

| Fichero | Qué es | Resultado |
|---------|--------|-----------|
| `tests/baseline/windows/baseline_windows_installed.txt` | Gauntlet sobre el **binario instalado hoy** en Windows (`C:\Program Files\Common Files\VST3\Opaline.vst3`) | 15 PASS / 1 FAIL |
| `tests/baseline/windows/baseline_windows.txt` | Gauntlet sobre el **build con los fixes** (`Build_Port/.../Opaline.vst3`) | 15 PASS / 1 FAIL |
| `tests/baseline/windows/pluginval_windows.txt` | pluginval `--strictness-level 10 --validate` sobre el build con fixes | **SUCCESS** (exit 0) |

Flags usados (idénticos a los que usará el job de Mac):
`--sr 48000 --block 512 --seed 20260918 --strict`.

**El único FAIL es `ALIASING_FLOOR` (-59.6 dB) y es PREEXISTENTE en Windows**: aparece igual en el
binario instalado, sin tocar por este port. Su origen es `Source/DSP/VoiceChain.cpp:497`
(`bias = 0.10f`, con el comentario "A/B en curso" del autor). **No se tocó**: cambiarlo alteraría el
contrato de Windows y queda como decisión de producto.

## 3. Fase 1 — Hallazgos de compilación y sus fixes

Cada fila: `archivo:línea` → **síntoma** → **fix** → **cómo se verificó que quedó**.

| # | Ubicación | Síntoma en Mac / en CI | Fix | Verificación |
|---|-----------|------------------------|-----|--------------|
| 1 | `CMakeLists.txt:16` (antes) | `add_subdirectory("/Users/Shared/JUCE")` → el configure **falla en Windows** y en cualquier runner de CI | `JUCE_PATH` portable (CMake → env → repo → `C:/JUCE`) con `FATAL_ERROR` explicativo | configure en Windows: exit 0 |
| 2 | `CMakeLists.txt:24` (antes) | `FORMATS VST3 AU Standalone` → JUCE linkea `AudioUnit`/`CoreAudioKit`, que no existen en Windows → **error de link** | AU solo con `if(APPLE)`; Windows queda `VST3 Standalone` | build Windows: exit 0 |
| 3 | `CMakeLists.txt:11` (antes) | `LANGUAGES CXX` → JUCE 9 exige el lenguaje C: *"A C compiler is required to build targets that depend on JUCE"* | `LANGUAGES C CXX` | configure: exit 0 |
| 4 | `Source/PluginEditor.cpp:18` | `juce::Drawable::createFromSVG(XmlElement)` **eliminada en JUCE 9** → `error C2039/C3861` | `createFromSVGString` (SVG crudo, mismo asset `BinaryData`) | build: exit 0 |
| 5 | `Source/PluginEditor.cpp:124,492` | `Drawable::setTransformToFit` ya no es miembro → `error C2039` | `drawWithin(g, rect, stretchToFit, 1.0f)`: **idéntico**, JUCE 9 lo implementa como `placement.getTransformToFit(getDrawableBounds(), destArea)` | 9/13 renders byte-idénticos (ver §5) |
| 6 | `CMakeLists.txt` (nuevo) | Sin universal ni deployment target → binario de una sola arquitectura y exige el macOS del runner | `if(APPLE)`: `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`, `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` | `verify_bundle.sh` (lipo) en el job de Mac |
| 7 | `CMakeLists.txt:78` + `Source/UI/Knob.cpp:55` | `EDITOR_WANTS_KEYBOARD_FOCUS FALSE` mientras el editor pide foco → en AU el host puede no darle el **foco inicial** y los atajos de los knobs no llegan | variable `OPALINE_EDITOR_WANTS_KEYBOARD_FOCUS`: `TRUE` solo en Apple, `FALSE` en Windows | barrido: PASS; Windows idéntico (gate de paridad) |
| 8 | `tests/snapshots` (target `snapshot`) | El harness no es target de `juce_add_plugin` → sin `JucePlugin_VersionString` → `error C2065` | `target_compile_definitions` con `${PROJECT_VERSION}` | build snapshot: exit 0 |
| 9 | `tools/*/CMakeLists.txt:6` | `add_subdirectory("C:/JUCE")` → **los self-tests no compilan en Mac** | `JUCE_PATH` portable en los 3 | los 3 compilan en Windows (exit 0) |
| 10 | `tools/dsp_selftest/main.cpp:19`, `tools/calibrate/main.cpp:31` | `fopen_s` es **solo MSVC** → no compila en clang | `#if defined(_MSC_VER)` + `fopen` | los 3 compilan y corren en Windows |
| 11 | `Source/**` | Intrínsecos x86, headers de Windows, `%APPDATA%`, `getSpecialLocation(windows)`, `std::mutex` en el hilo de audio | **ninguno** (barrido en 0 FAIL de código) | `audit_static.sh` |

### 3.1 Hallazgos del workflow ( Fase 2 ) — bugs que habrían roto el **primer** run en Mac

| # | Ubicación | Síntoma | Fix |
|---|-----------|---------|-----|
| 12 | workflow: descarga de pluginval | El asset real se llama **`pluginval_macOS.zip`** (O y S mayúsculas). El `grep -Eo '...macos...'` es case-sensitive → **no encuentra nada** → primer run roto | `grep -Eio` (verificado contra la API real de GitHub) |
| 13 | workflow: `upload-artifact` | JUCE deja el bundle en `build/<Name>_artefacts/Release/VST3/`. El glob `build/*.vst3` **no matchea nada** → artefacto vacío | `build/**/*.vst3` y `build/**/*.component` (verificado contra el layout real) |
| 14 | workflow: `find dist -maxdepth 3` | El bundle restaurado está a profundidad 5 → el job de firma **no encuentra nada** | `find dist -name ...` sin `-maxdepth` (verificado) |
| 15 | workflow: `VERSION=${GITHUB_REF_NAME#v}` | En `workflow_dispatch` el ref es la rama (`main`) → `pkgbuild` falla con una versión inválida. Y `productbuild` exige que la `version` del `distribution.xml` sea **exactamente** la del pkg | fallback a la versión del proyecto + `sed` del `version` en `distribution.xml` antes de `productbuild` |
| 16 | workflow: binario del Gauntlet | `find -name "gauntlet"` no matchea `Gauntlet` (case-sensitive) | `-iname "gauntlet*"` |
| 17 | `distribution.xml` | No existía → sin `choices-outline` ni payload, Apple rechaza el pkg ("could not be extracted") | creado con `choices-outline` + `pkg-ref` con **los mismos ids** que el workflow (`com.floodos.opaline.vst3` / `.au`) — verificado |
| 18 | `tools/compare_reports.py` | No existía → el gate de paridad no se podía correr en CI | copiado del skill |

### 3.2 Corrección al propio tooling del skill

| Ubicación | Problema | Fix |
|-----------|----------|-----|
| `Windows/.opencode/skills/macos-build-audit/scripts/compare_reports.py` | `DENORMAL_SILENCE.ratio` se comparaba como métrica **exacta** en modo `baseline` → FAIL falso. Medido 3 veces sobre **el mismo binario con el mismo seed**: 0.92 / 0.93 / 0.96 → es una medición de ruido, no un contrato de DSP | la métrica no determinista se informa (INFO) en ambos modos; el gate real de ese test es su veredicto `PASS`/`FAIL`, que sí se compara exacto |
| `Windows/.opencode/skills/macos-build-audit/scripts/audit_static.sh` | No entendía `EDITOR_WANTS_KEYBOARD_FOCUS` resuelto por variable con `if(APPLE)` → FAIL falso en cualquier plugin que use el patrón correcto | el barrido resuelve el ámbito (TRUE solo-Apple / TRUE siempre / FALSE) |
| `Windows/.opencode/skills/macos-build-audit/scripts/audit_static.sh` | No había forma de aceptar un FAIL correcto y documentado | excepciones en `.macos-audit-exceptions` (FAIL → WARN + razón a la vista); si el texto del aviso cambia, la excepción deja de aplicar |
| `Windows/.opencode/skills/macos-build-audit/reference/ci-macos.yml` | Los bugs #12-#16 estaban **también** en la plantilla canónica | corregidos en la plantilla y en Opaline |

## 4. Decisiones de producto (documentadas, no "por defecto")

1. **`JUCE_USE_CURL=0` en todas las plataformas.** La telemetría de instalación (`InstallPing`) ya es
   un no-op en Windows: hoy no sale nada a la red. Habilitarlo solo en Mac haría que los usuarios de
   Mac sí tuvieran tráfico de red saliente, invisible, y divergiría del comportamiento de Windows.
   Se acepta el WARN del barrido ("la red no funciona en ninguna plataforma") y se mantiene la
   paridad. *Si algún día se quiere la telemetría, hay que decidirlo para Windows y Mac a la vez.*
2. **`EDITOR_WANTS_KEYBOARD_FOCUS = TRUE` solo en Apple.** Los knobs son sliders accesibles
   (`flechas`/`Home`/`End` + Narrator). En Windows el valor sigue siendo `FALSE`, exactamente como
   estaba → 0 cambio en Windows.
3. **AU solo en Apple.** En Windows queda `VST3 + Standalone`.
4. **No se tocó** la matemática DSP, los defaults, los IDs ni el orden de parámetros, el layout de
   la UI ni el formato del estado.

## 5. Fase 3/4/5 en Windows (estado verificado)

| Gate | Resultado |
|------|-----------|
| Build Release (`Build_Port`) | **exit 0** |
| Build del harness de snapshots | **exit 0** |
| Gauntlet (16 tests) | **15 PASS / 1 FAIL** (`ALIASING_FLOOR`, preexistente) |
| pluginval `--strictness-level 10` | **SUCCESS** (exit 0) |
| `dsp_selftest` | **PASS** (la cadena es audible, `0.8163 → 0.4642`) |
| `bypass_test` | **PASS** (el motor no deriva al togglear bypass) |
| Snapshots de UI | 5 stories / 13 PNGs. **9/13 byte-idénticos** frente al render previo; 1 con delta ≤2; **3 con diff grande (84% de píxeles) por el cambio de tema sin commitear de otra sesión** (fondo `(14,16,19)` → `(6,7,8)`), **no** por la migración de `Drawable`. El logo renderiza bien. |

## 6. Fase 6 — Paridad (la prueba de que Windows no cambió)

```
python tools/compare_reports.py \
    tests/baseline/windows/baseline_windows_installed.txt \
    tests/baseline/windows/baseline_windows.txt --mode baseline
→ RESULTADO: 0 FAIL, 0 DIFF
```

Es decir: **el VST3 instalado hoy en Windows y el build con todos los fixes de este port son
indistinguibles** para el Gauntlet: mismo audio, misma latencia (10 muestras), mismos parámetros
(4), mismos bytes de estado (376), mismos layouts, 18 medidas de aliasing con el pico en el mismo
bin (20099.9 Hz), 0 clics, CPU equivalente.

## 7. Los 3 WARN del barrido (aceptados)

| WARN | Por qué se acepta |
|------|-------------------|
| Rutas absolutas de Windows en `CMakeLists.txt` | Es el **fallback** `C:/JUCE`, dentro de la resolución portable de `JUCE_PATH`. No se usa si el repo tiene JUCE o si se pasa `-DJUCE_PATH=`. |
| Hay `printf`/`std::cout` | Están en `tests/snapshots/main.cpp` (harness headless), **no** en `Source/` ni en el hilo de audio. El barrido excluye `tools/` pero no `tests/`. |
| `juce::URL` con `JUCE_USE_CURL=0` | Decisión documentada en §4.1. |

## 8. Qué falta (pendiente de un runner macOS)

1. **Primer run del CI en Mac** (`push` a `main` o `workflow_dispatch`): build universal, pluginval,
   render de snapshots, gate de paridad cross.
2. **Secrets** para el job `release`: `BUILD_CERTIFICATE_BASE64`, `INSTALLER_CERTIFICATE_BASE64`,
   `CERTIFICATE_PASSWORD`, `APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD`, `APPLE_TEAM_ID`.
3. **AuVal** (gate AU): requiere las *Audio Unit Tools* de Apple (descarga con login → no
   automatizable). El workflow deja el `.component` listo y lo dice.
4. **Gauntlet en Mac**: el harness no está vendorizado en el repo (vive en `Windows/tools/Gauntlet`).
   El workflow lo omite con aviso si no está; para el gate de paridad cross hay que compilarlo en
   Mac (es portable: solo hay que pasarle `GAUNTLET_JUCE_PATH`).
5. **Decisión sobre `ALIASING_FLOOR` (-59.6 dB)**: es un FAIL preexistente de Windows. Si se quiere
   cerrar, es un cambio de DSP **en ambos** sistemas (y hay que re-correr el baseline).
6. **Comparación de UI Mac↔Windows**: por estructura (mismo tamaño, mismo bbox, mismos widgets),
   no pixel a pixel: CoreText y DirectWrite rasterizan distinto.

## 9. Estado del repo

**Nada commiteado ni pusheado** (instrucción explícita). Los cambios quedan en el working tree:

- Modificados: `CMakeLists.txt`, `Source/PluginEditor.cpp` (migración JUCE 9 del logo),
  `tools/{dsp_selftest,bypass_test,calibrate}/CMakeLists.txt`,
  `tools/{dsp_selftest,calibrate}/main.cpp` (`fopen_s` portable).
- Nuevos: `.github/workflows/build-macos.yml`, `.github/scripts/verify_bundle.sh`,
  `distribution.xml`, `tools/compare_reports.py`,
  `tests/baseline/windows/{baseline_windows.txt,baseline_windows_installed.txt,pluginval_windows.txt}`,
  `MACOS_PORT.md` (este informe).
- **Preexistente de otra sesión y sin tocar**: el cambio de tema de la UI, `bias=0.10f` en
  `VoiceChain.cpp`, `InstallPing.cpp`, `fal_mark.svg` y el resto del diff previo de `Source/`.
