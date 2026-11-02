#!/usr/bin/env bash
# =============================================================================
#  verify_bundle.sh — verifica el binario de macOS (Fase 2 / G6)
#
#  Uso: bash verify_bundle.sh [build-dir] [deployment-target] [modo]
#   build-dir: dondecmake dejo los bundles (default: build)
#   deployment-target: minimo de macOS esperado (default: 11.0)
#   modo: "unsigned" (default) = verificacion PRE-firma (job build)
#         "signed"            = verificacion POST-firma (job release)
#
#  POR QUE EL MODO EXISTE:
#  En un binario UNIVERSAL, 'otool -L' imprime DOS bloques (uno por
#  arquitectura) y cada bloque arranca con la RUTA DEL BINARIO como titulo.
#  Con 'tail -n +2' solo se quita el titulo del PRIMER bloque: el titulo del
#  segundo bloque queda y se reporta como "dependencia externa" (FALSO POSITIVO).
#
#  Ademas, en el job BUILD el binario todavia NO esta firmado (la firma pasa
#  en el job RELEASE). Exigir alli 'hardened runtime' y 'codesign --verify'
#  produce 2 FALSO POSITIVOS garantizados. Con modo "unsigned" esos dos checks
#  son WARN; con modo "signed" son FAIL de verdad.
#
#  Exit: 0 = todo OK, 1 = hay FAIL
#  Compatible con bash 3.2 (macOS). Solo usa herramientas del sistema.
# =============================================================================
set -uo pipefail

BUILD="${1:-build}"
DEPLOY="${2:-11.0}"
MODE="${3:-unsigned}"

case "$MODE" in
    unsigned|signed) ;;
    *) echo "::error::modo invalido '$MODE' (usa 'unsigned' o 'signed')"; exit 2 ;;
esac

FAIL=0
PASS=0

emit() { # severity label detalle
    if [ "$1" = "FAIL" ]; then FAIL=$((FAIL + 1)); else PASS=$((PASS + 1)); fi
    echo "[$1] $2"
    if [ -n "${3:-}" ]; then printf '%s\n' "$3" | sed 's/^/         /'; fi
}

echo "================================================================"
echo " Verificacion del binario macOS — dir: $BUILD  (modo: $MODE)"
echo "================================================================"

if [ ! -d "$BUILD" ]; then
    echo "::error::No existe el directorio de build '$BUILD'"
    exit 1
fi

BUNDLES=$(find "$BUILD" \( -name '*.vst3' -o -name '*.component' \) -type d 2>/dev/null)
if [ -z "$BUNDLES" ]; then
    echo "::error::No se encontro ningun bundle (.vst3 / .component) en '$BUILD'"
    echo "        Revisar que FORMATS incluya VST3 y que el build haya terminado."
    exit 1
fi

echo
echo "Bundles encontrados:"
printf '%s\n' "$BUNDLES" | sed 's/^/  - /'
echo

for B in $BUNDLES; do
    echo "----------------------------------------------------------------"
    echo " $B"

    PLIST="$B/Contents/Info.plist"
    if [ ! -f "$PLIST" ]; then
        emit FAIL "falta Contents/Info.plist" ""
        continue
    fi

    # --- Info.plist -------------------------------------------------------
    if plutil -lint "$PLIST" >/dev/null 2>&1; then
        emit PASS "Info.plist valido"
    else
        emit FAIL "Info.plist corrupto" "$(plutil -lint "$PLIST" 2>&1)"
    fi

    for KEY in CFBundleIdentifier CFBundleExecutable CFBundleName CFBundlePackageType; do
        VAL=$(/usr/libexec/PlistBuddy -c "Print :$KEY" "$PLIST" 2>/dev/null || echo "")
        if [ -n "$VAL" ]; then
            emit PASS "$KEY = $VAL"
        else
            emit FAIL "Info.plist sin $KEY" ""
        fi
    done

    EXE_NAME=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$PLIST" 2>/dev/null || echo "")
    EXE="$B/Contents/MacOS/$EXE_NAME"
    if [ ! -f "$EXE" ]; then
        emit FAIL "no existe el ejecutable '$EXE'" ""
        continue
    fi

    # --- Universal --------------------------------------------------------
    if command -v lipo >/dev/null 2>&1; then
        ARCHS=$(lipo -archs "$EXE" 2>/dev/null || echo "")
        echo "         archs: ${ARCHS:-<none>}"
        case "$ARCHS" in
            *arm64*) emit PASS "contiene arm64 (Apple Silicon)" "" ;;
            *) emit FAIL "NO contiene arm64 (no corre en Apple Silicon)" "" ;;
        esac
        case "$ARCHS" in
            *x86_64*) emit PASS "contiene x86_64 (Intel)" "" ;;
            *) emit WARN "no contiene x86_64 (no corre en Intel)" "" ;;
        esac
    fi

    # --- Deployment target ------------------------------------------------
    if command -v vtool >/dev/null 2>&1; then
        BUILDINFO=$(vtool -show-build "$EXE" 2>/dev/null || echo "")
        MINOS=$(printf '%s\n' "$BUILDINFO" | sed -n 's/.*minos *\([0-9][0-9.]*\).*/\1/p' | head -1)
        if [ -n "$MINOS" ]; then
            echo "         minos: $MINOS (esperado >= $DEPLOY)"
            if [ "$(printf '%s\n%s\n' "$MINOS" "$DEPLOY" | sort -V | head -1)" = "$DEPLOY" ]; then
                emit PASS "deployment target >= $DEPLOY" ""
            else
                emit FAIL "deployment target $MINOS < $DEPLOY (no corre en el macOS minimo)" ""
            fi
        else
            emit WARN "no se pudo leer el deployment target (vtool)" ""
        fi
    fi

    # --- Dependencias -----------------------------------------------------
    # 'otool -L' en un binario universal imprime un bloque por arquitectura:
    #   <ruta-del-binario> (architecture x86_64):
    #       /usr/lib/...
    #   <ruta-del-binario> (architecture arm64):
    #       /usr/lib/...
    # Los titulos de bloque NO son dependencias. 'tail -n +2' solo quita el
    # primero -> el segundo titulo se cuela como dependencia FALSA.
    # La forma correcta: quedarse SOLO con las lineas INDENTADAS (las deps).
    if command -v otool >/dev/null 2>&1; then
        DEPS=$(otool -L "$EXE" 2>/dev/null | grep -E '^[[:space:]]' | awk '{print $1}' || true)
        BAD=$(printf '%s\n' "$DEPS" \
              | grep -vE '^(/usr/lib/|/System/|@rpath/|@executable_path/)' || true)
        if [ -z "$BAD" ]; then
            emit PASS "todas las dependencias son del sistema o relativas al bundle"
        else
            emit FAIL "dependencias externas (rompen Gatekeeper o no existen en la maquina del usuario)" "$BAD"
        fi
        # Un '@rpath/...' tiene que existir DENTRO del bundle: si no, el
        # plugin no abre en la maquina del usuario (aunque compile bien).
        RPATH_MISSING=""
        for d in $DEPS; do
            case "$d" in
                @rpath/*)
                    LEAF="${d#@rpath/}"
                    if [ ! -e "$B/Contents/Frameworks/$LEAF" ] && [ ! -e "$B/Contents/MacOS/$LEAF" ]; then
                        RPATH_MISSING="$RPATH_MISSING$d\n"
                    fi
                    ;;
            esac
        done
        if [ -n "$RPATH_MISSING" ]; then
            emit FAIL "dependencias @rpath que NO existen dentro del bundle" "$RPATH_MISSING"
        elif printf '%s\n' "$DEPS" | grep -q '@rpath/'; then
            emit PASS "las dependencias @rpath existen dentro del bundle"
        fi
    fi

    # --- Firma ------------------------------------------------------------
    # Modo "unsigned" (job build): la firma todavia no existe. Serian 2
    # FALSOS POSITIVOS garantizados. Solo se informa.
    # Modo "signed" (job release): aqui SI tiene que estar firmado.
    if codesign -dv "$B" >/dev/null 2>&1; then
        SIGINFO=$(codesign -dv --verbose=2 "$B" 2>&1 || true)
        if printf '%s\n' "$SIGINFO" | grep -q 'flags=.*runtime'; then
            emit PASS "firmado con hardened runtime (requisito de notarizacion)"
        elif [ "$MODE" = "signed" ]; then
            emit FAIL "firmado SIN hardened runtime (--options runtime): Apple no lo va a notarizar" \
                 "$(printf '%s\n' "$SIGINFO" | grep -E '^(Identifier|TeamIdentifier|flags)' || true)"
        else
            emit WARN "sin hardened runtime todavia (esperado: la firma pasa en el job release)" \
                 "$(printf '%s\n' "$SIGINFO" | grep -E '^(Identifier|TeamIdentifier|flags)' || true)"
        fi
        if codesign --verify --deep --strict --verbose=2 "$B" >/dev/null 2>&1; then
            emit PASS "codesign --verify --deep --strict OK"
        elif [ "$MODE" = "signed" ]; then
            emit FAIL "codesign --verify --deep --strict fallo" \
                 "$(codesign --verify --deep --strict --verbose=2 "$B" 2>&1 | head -5)"
        else
            emit WARN "codesign --verify todavia no aplica (el bundle se firma en el job release)" \
                 "$(codesign --verify --deep --strict --verbose=2 "$B" 2>&1 | head -3)"
        fi
    elif [ "$MODE" = "signed" ]; then
        emit FAIL "el bundle NO esta firmado (modo signed: la firma tiene que existir)" ""
    else
        emit WARN "sin firma (esperado en el job build: la firma pasa en el job release)" ""
    fi

    # --- Ticket de notarizacion (opcional) -------------------------------
    if command -v xcrun >/dev/null 2>&1; then
        if xcrun stapler validate "$B" >/dev/null 2>&1; then
            emit PASS "ticket de notarizacion stapled"
        else
            emit WARN "sin ticket de notarizacion stapled (normal antes de la fase de release)" ""
        fi
    fi
done

# --- pkg final (si esta) ---------------------------------------------------
PKGS=$(find "$BUILD" . -maxdepth 2 -name '*.pkg' -type f 2>/dev/null | head -5)
for P in $PKGS; do
    echo "----------------------------------------------------------------"
    echo " $P"
    if command -v pkgutil >/dev/null 2>&1; then
        if pkgutil --check-signature "$P" >/dev/null 2>&1; then
            emit PASS "pkg firmado correctamente"
        else
            emit FAIL "pkg sin firma valida" "$(pkgutil --check-signature "$P" 2>&1 | head -5)"
        fi
    fi
done

echo
echo "================================================================"
printf ' RESULTADO: %d FAIL, %d PASS\n' "$FAIL" "$PASS"
echo "================================================================"
if [ "$FAIL" -gt 0 ]; then
    echo "::error::$FAIL verificaciones fallidas"
    exit 1
fi
echo "::notice::Binario verificado."
exit 0
