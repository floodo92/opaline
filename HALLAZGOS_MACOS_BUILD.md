# Hallazgos de build macOS — Opaline

> **Documento de traspaso** (generado 2026-11-01 con el skill `macos-build`, repo privado
> `floodo92/macos-build`). Sirve para retomar el trabajo **desde otra sesión**: cada hallazgo
> trae el síntoma exacto, la causa, el **fix con archivo y línea**, y cómo verificar que quedó
> arreglado.
>
> **Regla 0: Windows es sagrado.** Ningún fix de este documento cambia cómo suena ni cómo se
> comporta el plugin en Windows: son cambios de **workflow/CI**, no de DSP.
>
> **Este repo es el que mejor está**: su workflow tiene **todos** los gates del skill. Lo que queda
> son 2 huecos puntuales y el run real en macOS.
>
> **Documento complementario:** `MACOS_PORT.md` (la auditoría de DSP/UI/paridad hecha con el skill
> `macos-build-audit`). Este archivo es el complemento de **build/firma/entrega**.

---

## 0. Cómo continuar (2 minutos)

1. Correr el preflight:
   ```bash
   bash "F:/MARCELO/Software/Plugins/Floodos audio labs/Plugins/Windows/.opencode/skills/macos-build/scripts/preflight.sh" .
   ```
2. Aplicar **H2** (el `auval` que no valida) y revisar **H1** (red desactivada: decisión, no bug).
3. Re-correr el preflight: debe dar **0 FAIL** (ya lo da).
4. **H3 es el paso que falta de verdad**: correr el workflow en un runner macOS.

---

## 1. Estado

| Gate | Estado | Evidencia |
|------|--------|-----------|
| G1 binario universal | OK | `build-macos.yml:88` (`arm64;x86_64`) |
| G2 build optimizado (Release) | OK | `build-macos.yml:86` (`-DCMAKE_BUILD_TYPE=Release`) |
| G3 el `.pkg` tiene payload | OK | `build-macos.yml:303-307` (check antes de notarizar) |
| G4 el `.pkg` está firmado | OK | `build-macos.yml:276-280` (identidad **leída** del keychain) |
| G5 está notarizado | OK | `build-macos.yml:322-328` (`--output-format json` + staple) |
| G6 Gatekeeper | OK | `build-macos.yml:329` (`spctl`) |
| keychain fail-fast | OK | `build-macos.yml:233` (`security find-identity`) |
| JUCE pineado | OK | `build-macos.yml:78` (`--branch "$JUCE_VERSION"`, L38 = `9.0.4`) |
| firma solo con tag/manual | OK | `build-macos.yml:204` (guardia del job `release`) |
| pluginval real | OK | `build-macos.yml:120-126` (strictness 10, asset real) |
| **auval real** | **NO** | `build-macos.yml:128-133` → **H2** |
| CI en verde | **sin run real** | nunca se ejecutó en macOS → **H3** |

---

## 2. Hallazgos abiertos

### H1 — WARN — el plugin usa `juce::URL` pero la red está desactivada a propósito

**Dónde:** `Source/InstallPing.cpp:47-64` (`juce::URL` + `createInputStream`) y
`CMakeLists.txt:116` y `:153` (`JUCE_USE_CURL=0`).

**Qué significa:** **no es un bug de port** — es una decisión de producto: el ping de licencia no
funciona en **ninguna** plataforma (Windows tampoco). Por eso el preflight lo reporta como WARN y
no como FAIL.

**Por qué importa igual:** si algún día se decide reactivar la red, hay que quitar
`JUCE_USE_CURL=0` **en las dos plataformas** y poner `NEEDS_CURL TRUE`; si se queda como está, el
plugin tiene que fallar **de forma visible** cuando no puede hacer el ping (hoy, sin red, el error
puede ser silencioso).

**Qué hacer (opcional, sin apuro):**
- Dejar la decisión escrita donde se vea (ya está en `MACOS_PORT.md` §1, fila *"Red / licencia"*).
- O dejar que el ping falle con un mensaje explícito ("licencia: sin conexión") en vez de
  colgarse en silencio.

**Cómo verificar:** que el documento de release o el README digan explícitamente
*"el ping de licencia no está activo en ninguna plataforma"*.

---

### H2 — WARN — el step de `auval` **no valida nada** (es un `echo`)

**Dónde:** `.github/workflows/build-macos.yml:128-133`

```yaml
      - name: Validar AU (AuVal) — MANUAL
        if: always()
        run: |
          echo "::notice::El .component se genera en build/*.component"
          find build -name "*.component" -type d -print || true
          echo "::notice::AuVal requiere la Audio Unit Tools de Apple (descarga manual)."
```

**Síntoma:** el job dice "Validar AU" y sale verde, pero **no se valida ningún Audio Unit**: solo
imprime dónde está el `.component`. Si el AU no registra, no compila o tiene un problema de
`Info.plist`, la CI **no lo detecta**.

**Causa:** `auval` es una herramienta de línea de comandos que **ya viene con macOS** (no hace falta
la "Audio Unit Tools" de Apple): lo que hay que hacer es **instalar** el `.component` y validarlo
por **type/subtype/manufacturer** (E19 del catálogo). Es el mismo error de clase que "el pluginval
del CI no validaba nada" (E27), pero aplicado a `auval`.

**Fix** (reemplazar el step):

```yaml
      - name: Validar AU (auval)
        if: always()
        run: |
          set -euo pipefail
          COMPONENT=$(find build -name "*.component" -type d -print -quit)
          if [ -z "$COMPONENT" ]; then
            echo "::notice::No se genero .component (este proyecto no declara AU)"
            exit 0
          fi
          # auval NO acepta una ruta: hay que INSTALAR el .component y validarlo
          # por type/subtype/manufacturer:
          #   aufx = efecto de audio | Opln = subtipo | FloA = fabricante
          # (Opln/FloA son los codigos REALES de Opaline: CMakeLists.txt:71-72)
          mkdir -p ~/Library/Audio/Plug-Ins/Components/
          cp -R "$COMPONENT" ~/Library/Audio/Plug-Ins/Components/
          auval -v aufx Opln FloA
```

> **Los 3 códigos tienen que ser los reales del plugin.** Para Opaline son `aufx` (efecto),
> `Opln` (subtipo) y `FloA` (fabricante) — salen de `CMakeLists.txt:71-72`
> (`PLUGIN_MANUFACTURER_CODE FloA`, `PLUGIN_CODE Opln`). Para **verificar** contra el bundle:
> ```bash
> /usr/libexec/PlistBuddy -c 'Print :AudioComponents' "<bundle>/Contents/Info.plist"
> ```

**Cómo verificar:** el log del step tiene que mostrar la salida de `auval` (con el nombre del AU y
sus parámetros), no solo el `find`.

---

### H3 — INFO — nunca se ejecutó el workflow en un runner macOS

**Dónde:** `.github/workflows/build-macos.yml` (workflow completo, **nunca corrido**).

**Qué significa:** el workflow está escrito y validado sintácticamente, pero **no hay un run verde**
que pruebe que compila, firma y notariza. `MACOS_PORT.md` lo dice explícitamente:
*"Veredicto: NO PUBLICABLE TODAVÍA — falta el run en macOS (Fases 2-6 en Mac)"*.

**Qué falta para cerrarlo:**

| Fase | Qué | Estado |
|------|-----|--------|
| 2 | el workflow corre y el build es universal | pendiente del 1er run |
| 3 | DSP verificado en Mac | pendiente (el Gauntlet de Mac) |
| 4 | UI verificada en Mac | pendiente (el harness de snapshots ya renderiza headless) |
| 5 | validación runtime en Mac | pendiente (pluginval + auval + self-tests) |
| 6 | gate de paridad Mac↔Windows | pendiente (compara contra `tests/baseline/windows/baseline_windows.txt`) |

**Cómo continuar:**
1. `gh workflow run build-macos.yml --repo floodo92/opaline` (disparo manual, **sin** `notarize`).
2. Mirar el log: el build tiene que ser universal y los self-tests verdes.
3. Cuando el build esté validado, repetir con `notarize: true` para producir el `.pkg`.
4. Comparar el informe de paridad contra el baseline congelado de Windows.

---

### H4 — INFO — el nombre del workflow no sigue la convención del skill

**Dónde:** `.github/workflows/build-macos.yml` (el skill y el resto de la familia usan `build-mac.yml`).

**Qué significa:** el preflight lo encuentra igual (hace glob de `*.yml`), pero cualquier
documentación o script que apunte a `build-mac.yml` no lo va a encontrar. Renombrar es opcional.

---

## 3. Lo que YA está bien — no tocar (es el modelo a copiar)

| Qué | Dónde | Por qué está bien |
|-----|-------|-------------------|
| `CMAKE_BUILD_TYPE=Release` | `build-macos.yml:86` | en Mac el generador es single-config (E16) |
| Universal + deployment target | `build-macos.yml:88` + `:89` | arm64 + x86_64 (E7) |
| JUCE pineado por `env: JUCE_VERSION` | `build-macos.yml:38,78` | clonar `master` rompe la API sin avisar (E15) |
| `find` para los paths de artefacto | `build-macos.yml:123` | nunca hardcodear rutas (E20) |
| pluginval **real** (strictness 10) | `build-macos.yml:120-126` | no un `echo` (E27) |
| Identidad **leída** del keychain | `build-macos.yml:234,276` | nada de nombres hardcodeados (E32) |
| Firma de adentro hacia afuera | `build-macos.yml:228-247` | `--deep` firma en un orden que Apple rechaza (E31) |
| Notariza los bundles con `ditto` (zip) | `build-macos.yml:248-269` | `notarytool` no acepta un directorio |
| `notarytool --output-format json` | `build-macos.yml:261,324` | sin esto el error dice "status: Invalid" y nada más (E40) |
| Chequeo de payload antes de notarizar | `build-macos.yml:303-307` | no gastar una notarización en un pkg vacío (E33) |
| `stapler staple` + `validate` | `build-macos.yml:267-268,327-328` | sin el ticket, Gatekeeper rechaza el pkg (E41) |
| **`release` job con guardia** | `build-macos.yml:204` | `startsWith(github.ref, 'refs/tags/v') \|\| inputs.notarize` → **la firma solo corre con tag o a pedido**; el push normal no gasta cuota de Apple |
| Cache de JUCE por versión | `build-macos.yml:67-75` | no re-clonar en cada run |
| Gate de paridad contra baseline congelado | `build-macos.yml:174` | una regresión de DSP en Mac se ve como regresión |
| `JUCE_USE_CURL=0` explícito | `CMakeLists.txt:116,153` | la red desactivada es **consciente**, no un descuido |

---

## 4. Errores ya corregidos (historia — no repetirlos)

| Dónde | Qué se resolvió |
|-------|-----------------|
| `MACOS_PORT.md` §2-§6 | auditoría completa con `macos-build-audit`: baseline de Windows congelado, 15 PASS / 1 FAIL, pluginval SUCCESS, gate de paridad en verde |
| `Source/InstallPing.cpp` | el ping de licencia usa rutas portables (`userApplicationDataDirectory`, que en Mac es `~/Library/Application Support`, no `%APPDATA%`) |
| `CMakeLists.txt:116,153` | `JUCE_USE_CURL=0` explícito → sin red en ninguna plataforma, a propósito |
| `build-macos.yml` | el workflow completo (build universal + validación + release con firma) |
| `Source/DSP/VoiceChain.cpp:886` | **E49/P1** — `os->processSamplesDown (juce::dsp::AudioBlock<float> (b));` enlazaba un temporal a una referencia **no-const**. MSVC lo acepta, clang lo rechaza. Ahora usa un bloque con nombre (`portBlock`). Equivalencia exacta, verificado que **sigue compilando en Windows**. |
| `.github/scripts/verify_bundle.sh` | **E50/E51/E52** — 3 bugs: (a) `otool -L \| tail -n +2` reportaba la ruta del binario como dependencia en binarios universales; (b) el job `build` exigía la firma antes de que el job `release` firmara (2 falsos positivos garantizados); (c) `@rpath` sin resolver. Ahora el script tiene **2 modos**: `unsigned` (job build) y `signed` (job release, después de firmar). |
| `.github/workflows/build-macos.yml` | **E53** — el binario de pluginval **no** está en la raíz del zip de macOS (`pluginval.app/Contents/MacOS/pluginval`). Se busca por nombre con `find`. |
| Secrets de GitHub | los 6 secrets cargados en `floodo92/opaline`. Verificado que los 2 certificados tienen **claves privadas DISTINTAS** (sin riesgo E29: `productsign` colgado). |

### Runs reales (la historia completa)

| Run | Qué falló | Clase | Arreglado |
|-----|-----------|-------|-----------|
| `41368971002` | `Compilar` — temporal a referencia no-const | **E49/P1** | `68e3763` |
| `41375076151` | `Verificar el binario` — 6 falsos positivos | **E50/E51** | `22adcd8` |
| `41377071502` | `Instalar pluginval` — no está en la raíz del zip | **E53** | `36b350a` |

---

## 5. Verificación completa

```bash
# 1. preflight local (debe dar 0 FAIL)
bash "<Windows>/.opencode/skills/macos-build/scripts/preflight.sh" .

# 2. en el runner macOS, tras el workflow:
lipo -info "<bundle>/Contents/MacOS/<CFBundleExecutable>"   # arm64 x86_64
pkgutil --expand final_plugin.pkg .pkgcheck && find .pkgcheck -name Payload -size +1k
pkgutil --check-signature final_plugin.pkg
xcrun stapler validate final_plugin.pkg
spctl -a -vvv -t install final_plugin.pkg

# 3. el AU tiene que registrarse:
mkdir -p ~/Library/Audio/Plug-Ins/Components/ && cp -R "<bundle>.component" ~/Library/Audio/Plug-Ins/Components/
auval -v aufx Opln FloA
```

---

## 6. Dónde está la evidencia

| Qué | Dónde |
|-----|-------|
| Skill (templates, catálogo de errores, scripts) | `F:\MARCELO\Software\Plugins\Floodos audio labs\Plugins\Windows\.opencode\skills\macos-build\` |
| Catálogo de errores E1-E42 | `floodo92/macos-build` → `reference/error-catalog.md` |
| **Auditoría de DSP/UI/paridad** (este repo) | `MACOS_PORT.md` |
| Certificados y secretos | `C:\Users\MarMaus\Desktop\Certificados APPLE\` (`LEEME.txt` + `secretos\`) |
| Reglas de firma/notarización | `floodo92/macos-build` → `reference/signing-notarization.md` |
| Checklist de cierre / qué chequea el tester | `floodo92/macos-build` → `reference/checklist.md` |
| Baseline congelado de Windows | `tests/baseline/windows/baseline_windows.txt` |
