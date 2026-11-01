# BRIEF — Mockup UI de **OPALINE PRO** (generar HTML/CSS)

Pegar todo este documento como prompt a una IA de diseño/código. El objetivo es producir **un único
archivo HTML autocontenido** que sirva como **maqueta de concepto** de la versión Pro de Opaline, y que
después se pueda portar **1:1 a C++/JUCE** (`juce::Graphics`).

No es una maqueta libre: es una especificación visual ejecutable.

---

## 0. Contexto de producto (leer antes de diseñar)

**Opaline** es un plugin de audio (VST3, Windows) de la serie free de *floodo's audio labs*. Es un
**polisher vocal de un solo knob**: un knob `SHINE` (0…100) escala una cadena de 10 etapas
(hipass adaptativo → corte low-mid → de-esser → compresor óptico → supresor de resonancia →
lodos → exciter armónico → air shelf → bus paralelo → tape glue), y un botón `MEASURE` analiza la
voz real y calibra sola los centros y las sensibilidades.

**La doctrina** (esto es lo importante): Opaline no es un plugin de brillo, es la implementación
automática de una regla de mezcla — el **triángulo espectral**:

- **Cuerpo 500–1000 Hz = B** (la referencia, el vértice del triángulo)
- **Graves ~200 Hz = B − 5 dB**
- **Agudos 2–8 kHz = B − 5 dB**
- **Sibilancias 5–10 kHz = entre B − 5 y B** (puede llegar a igualar B en vocal agresiva)

En el plugin **gratis esa doctrina es invisible**: el usuario gira un knob y ya está. En la **versión
Pro** el usuario **la ve**. Ese display es el corazón del producto y la razón de compra.

### Qué cambia en el Pro (esto es lo que la maqueta tiene que comunicar)

El free vende **el sonido** (MEASURE + 1 knob). El Pro vende **la confianza en una sesión**:

1. **Ver el triángulo** — display en vivo: espectro real vs. objetivo de la doctrina.
2. **Deshacer el MEASURE** — los 3 centros medidos se pueden corregir a mano.
3. **Etapas auditables** — bypass/solo por etapa (así trabaja un ingeniero).
4. **Match contra referencia** — medir otra fuente (la mezcla) y matchear el vocal contra ella.
5. **Controles que el free ata al knob** — `GLUE` y `MIX` salen del ride de SHINE.

### Lo que la maqueta NO debe ser

- **No** es "Opaline con 12 knobs sueltos". Sigue siendo **un plugin de un knob con un visor**.
- **No** es una consola genérica de 30 controles. Jerarquía fuerte: el visor y el SHINE mandan.
- **No** cambia la identidad visual: tiene que leerse como **el mismo producto** que el free, más
  grande y más serio.

---

## 1. REGLA DE ORO — subset implementable en JUCE

Todo lo que uses tiene que poder dibujarse en JUCE con la API de `Graphics`: fills, gradientes
lineales/radiales simples, rectángulos redondeados, líneas de 1px, texto y paths vectoriales.

### PROHIBIDO (rompe el port a JUCE)

- `backdrop-filter`
- `filter: blur()` / `drop-shadow()`
- `mix-blend-mode`
- `conic-gradient`
- ruido o texturas (grain, `repeating-linear-gradient` usado como ruido)
- animaciones, `transition`, `transform` 3D / perspectiva
- **más de una capa de `box-shadow`** (una sola, blur ≤ 16px, solo para sombra de panel)
- imágenes externas, SVG con `<filter>`/`<mask>`/`<pattern>`, icon fonts
- glifos que no sean texto

### PERMITIDO

- `background-color`, `linear-gradient` (2–4 stops), `radial-gradient` (simple)
- `border`, `border-radius`, hairlines de 1px
- una `box-shadow` de baja difusión
- `rgba()`
- `font-family` / `font-size` / `font-weight` (400/500/600/700) / `letter-spacing` / `text-transform`
- `::before` / `::after` solo para líneas o rects planos
- **un `<svg>` acotado** para el display del triángulo: únicamente `<path>`, `<polyline>`,
  `<line>`, `<rect>` y `<text>`, sin filtros, sin masks, sin defs. (En JUCE es un `juce::Path`
  stroked/filled — es la equivalencia 1:1 exacta.)

---

## 2. Canvas

- Exactamente **880 × 540 px**, fijo. No responsive, sin media queries.
- Sin scroll, sin márgenes: `body` de 880×540 exactos.
- Se va a screenshotear con Chrome headless a 1× y 2×.
- Fondo del plugin: `#060708`.

*(El free es 400×460 cuadrado. El Pro crece a 880×540 apaisado: el visor del triángulo necesita
ancho de frecuencia, y las 4 columnas de módulos necesitan lugar. Que se lea como el hermano mayor.)*

---

## 3. Sistema de diseño — **idéntico al free, no inventar nada**

Estética declarada: **"maison de alta costura"** — grafito profundo, acento ópalo iridiscente,
hairlines finísimas, tipografía editorial, **cero glow barato**.

Usar exactamente estos tokens. No cambiar la paleta.

### Superficies (grafito)
| token | valor |
|---|---|
| bg | `#060708` |
| surface | `#0e1013` |
| panel | `#12151a` |
| panelMid | `#181b21` |
| raised | `#1d2129` |
| inset | `#050607` |

### Acento — ópalo iridiscente
**Solo como acento fino. Nunca como relleno grande ni como gradiente dominante.**

| token | valor |
|---|---|
| accent | `#cfc6f2` |
| opalWarm | `#e8c8a8` |
| opalCold | `#9fb8e8` |

### Hairlines (blanco con alpha)
`faint 5.5%` · `normal 9.5%` · `strong 17%`

### Texto
| token | valor |
|---|---|
| textHi | `#eceef1` |
| textMid | `#9aa1ab` |
| textDim | `#7b838d` |
| textLo | `#5a626c` |

`textLo` es solo decorativo (no hay que leerlo). Texto chico siempre `textDim` o superior.

### Semánticos — SOLO meters, deltas fuera de objetivo y acciones peligrosas
`safe #52c49a` · `warn #e0ad5a` · `danger #e06a6a`

### Radios
`3 / 5 / 8 / 10 px`

### Metal de knobs
`hi #a9a9b3` · `mid #75757f` · `lo #484852` — **acero, no cromo**. Nada de reflejos duros.

### Tipografía (las 3 webfonts disponibles)
- **Inter** → UI y labels funcionales
- **Barlow Condensed** → display: títulos, headers de módulo, MAYÚSCULAS con tracking amplio
- **JetBrains Mono** → valores numéricos y readouts (tabular)

Escala tipográfica a respetar (alto, de la familia Barlow):
- 22px bold, tracking 0.16em → título del producto
- 12px, tracking 0.24em → header de módulo
- 10px, tracking 0.16em → sublabel de knob
- 11.5px Inter → valor numérico y body
- 8.5px Mono → ticks, unidades, axis labels

---

## 4. Dirección estética: INSTRUMENTO DE PRECISIÓN

Sobriedad de superficies de casa editorial, con un visor técnico que es el protagonista.

- **El visor del triángulo es lo único con color de verdad**: el espectro en un gradiente
  `opalCold → opalWarm` de 1px, y la línea objetivo de la doctrina en `accent`. Todo lo demás es
  grafito + hairlines.
- **Contraste alto donde importa**: valores numéricos, dB del visor, meters. Nítidos, nunca
  difuminados.
- **El ópalo es un secreto**: líneas finas, indicadores, estado activo. Nunca como pared de color.
- Referencias de mercado: **FabFilter Pro-Q 3, iZotope Nectar 3, Sonible Smart:comp, Valhalla
  VCV, Waves Clarity Vx Pro**. Calmado, legible, caro.
- **Sin=cero íconos de sistema** (no uses emojis ni Unicode decorativo tipo ⚡). Los estados se
  comunican con texto en mayúsculas y color.

---

## 5. EL VISOR — `TRIANGLE` (lo más importante de la maqueta)

Un panel de **854 × 190 px** (inner), fondo `inset`, borde hairline `faint`, radio 8px.
Es el hero visual y tiene que quedar legible aunque no tenga datos reales.

### Ejes

- **X = frecuencia logarítmica, 40 Hz → 20 kHz**. Gridlines verticales hairline en
  `100 · 500 · 1k · 2k · 5k · 10k`, label en JetBrains Mono 8.5px `textLo`, alineado al piso.
- **Y = dB relativo**, rango **−18 … +18 dB**, con **0 dB = B** (la referencia del cuerpo).
  Gridlines horizontales en `0 · −5 · −10 · +5` dB, hairline `faint`. La línea de **0 dB** se
  dibuja en `accent` al 30% (es la referencia B, tiene que verse).
- En el margen izquierdo, labels `+10` `0` `−10` en Mono 8.5px `textLo`.

### Tres capas, en este orden de atrás hacia adelante

1. **Objetivo de la doctrina** (el "triángulo") — la capa que da nombre al producto.
   Un **path `<polyline>` cerrado de 1px en `accent` al 55%**, con esta forma (en dB relativos,
   leída de izquierda a derecha: 200 Hz, 500 Hz, 1 kHz, 2–8 kHz, 10 kHz):
   ```
   (200 Hz)   −5 dB
   (500 Hz)    0 dB   ← el vértice, B
   (1 kHz)     0 dB   ← meseta del cuerpo (la "B" es la banda 500–1k entera)
   (2 kHz)    −5 dB
   (8 kHz)    −5 dB
   (10 kHz)   −3.2 dB ← techo de sibilancia: puede subir hasta B en vocal agresiva
   ```
   Cerrar el path por la línea de piso. Relleno con `accent` al **4%**.
   Etiquetar el vértice con `B` en Barlow 10px `accent` arriba del punto.

2. **Espectro real en vivo** — `<polyline>` de 1.25px con un `linear-gradient` horizontal
   `opalCold → accent → opalWarm` (se aplica con `stroke` o con dos `<polyline>` superpuestos si
   hace falta). Relleno debajo con el mismo gradiente a **7%** de alpha, desvanecido a 0 en 40px.
   La curva tiene que verse **claramente fuera del objetivo en la zona de sibilancia** (por
   ejemplo asomando 4 dB por encima en 6–8 kHz): así se ve que el plugin tiene trabajo que hacer.

3. **Marcadores de banda** — por cada banda, un bracket horizontal hairline `strong` de 1px,
   ancho 1px de alto 6px en cada extremo, más el label:
   `LOWS 200` · `BODY 500–1k` · `HIGHS 2–8k` · `SIB 5–10k`
   Label en JetBrains Mono 8.5px `textMid` sobre la banda, y **a la derecha** un readout del
   delta en dB: `S +4.1` — en `warn` si está fuera de objetivo, en `safe` si está dentro.

### Readout de medición (a la derecha del display, columna de 150px)

Cuatro filas, cada una: nombre de banda en Barlow 9.5px `textMid` MAYÚSCULAS, y a la derecha el
delta en Mono 11.5px con su color semántico. Formato `+4.1 dB` / `−1.2 dB` / `IN`.

```
LOWS    200     −1.8 dB   safe
BODY    500–1k   IN       safe     ← la referencia, siempre "IN" (es la B)
HIGHS   2–8k    −0.4 dB   safe
SIB     5–10k   +4.1 dB   warn
```

Debajo, una línea hairline y un dato de calibración en Mono 8.5px `textDim`:
`MEDIAN PITCH 118 Hz · 2 214 WINDOWS`

### Header del visor

Barra de 20px sobre el display: a la izquierda `TRIANGLE` en Barlow 12px `textHi` tracking
0.24em MAYÚSCULAS. A la derecha, un selector de modo de 3 segmentos chiquitos (TRIANGLE ·
SPECTRUM · OFF), 1px hairline `normal`, texto Mono 8.5px `textDim`, con el segmento activo en
`textHi` + `background-color: rgba(207,198,242,0.10)` + borde `accent` al 40%.

---

## 6. Controles REALES — no inventar ninguno

Estos son todos los controles del Pro. Nombres exactos **en inglés**.

### Módulo `SHAPE` — el control principal (col A)
| # | Control | Rango | Default | Notas |
|---|---|---|---|---|
| 1 | Knob **SHINE** | 0 … 100 | **45** | hero, diámetro **132px**. Es el control más grande de la maqueta después del visor. Unipolar, con arco de valor |
| 2 | Knob **BODY / AIR** | −100 … +100 | 0 | **bipolar**, diámetro 78px, indicador de centro (tick a las 12) marcado |

### Módulo `CENTERS` — deshacer el MEASURE (col B)
Tres knobs **pequeños (58px)**, apilados verticalmente. Cada uno es un **offset bipolar** sobre el
centro que midió MEASURE: el valor medido real se muestra debajo en Mono 8.5px `textDim`, para que
se entienda que el knob corrige lo medido.

| # | Control | Rango | Default | Lectura debajo |
|---|---|---|---|---|
| 3 | **MUD** | −100 … +100 | 0 | `200–500 Hz · 320` |
| 4 | **SIB** | −100 … +100 | 0 | `5–10 kHz · 6800` |
| 5 | **RESO** | −100 … +100 | 0 | `2–5 kHz · 3400` |

Debajo de los tres, una línea hairline y un texto de estado de la medición en Mono 8.5px
`safe` o `warn`: `MEASURED · 2 214 WINDOWS` o `NOT MEASURED — PRESS MEASURE`.

### Módulo `GLUE` (col C)
| # | Control | Rango | Default | Notas |
|---|---|---|---|---|
| 6 | Knob **GLUE** | 0 … 100 | 55 | diámetro 78px. Sale del ride de SHINE (en el free el tape va atado al knob) |
| 7 | Knob **MIX** | 0 … 100 | 40 | diámetro 78px. Bus paralelo, dry ↔ denso |
| 8 | Knob **OUTPUT** | 0 … 100 | 70 | diámetro 78px |

### Módulo `CHAIN` — auditoría por etapa (col D)
Siete toggles pequeños, **26 × 20px cada uno**, en dos columnas, con label en Barlow 9px `textDim`
MAYÚSCULAS a la izquierda. Todos **activos** por defecto. Al estar activo: texto `textHi` + fondo
`rgba(207,198,242,0.10)` + borde `accent` 30%. Al estar bypasseado: texto `textLo`, fondo `panelMid`,
borde hairline `faint`, y **el texto tachado con una línea de 1px `danger` al 50%**.

Orden exacto:
```
EQ        DE-ESS     OPTICAL    RESO
EXCITER   PAR        TAPE
```

### Botones del header
Cuatro botones **pill**, 1px hairline, Barlow 10px tracking 0.16em MAYÚSCULAS, 22px de alto:

| Botón | Estado por defecto | Aspecto del estado activo |
|---|---|---|
| **BYPASS** | off | texto `textMid`, fondo `panelMid` |
| **MEASURE** | off | texto `textHi`, fondo `rgba(207,198,242,0.14)`, borde `accent` 45% |
| **MATCH** | off | idem MATCH; además muestra un LED de 5px `safe` a la izquierda |
| **A/B** | off (escucha wet) | idem; es un **hold** (mantener = seco) |

### Meters (columna slim derecha, x ≈ 856…864)
Dos barras verticales de 8px de ancho, 236px de alto, con:
- gridlines horizontales hairline `faint` en −6 / −18 / −36 dBFS
- barra con gradiente vertical `safe → warn → danger`
- peak-hold: línea horizontal de 1px `textHi` al 80%
- label **IN** / **OUT** en Barlow 9px `textMid` debajo, y lectura de pico en Mono 8.5px `textDim`

### Header del producto
- Izquierda: `OPALINE PRO` en Barlow 22px bold tracking 0.16em `textHi`, y debajo
  `VOCAL POLISH · DOCTRINE` en Inter 9.5px `textDim` tracking 0.18em MAYÚSCULAS.
- Derecha, antes de los botones: un campo de preset — rectángulo `inset` con borde hairline, radio 5px,
  ancho 148px, que muestra el nombre en Inter 11px `textHi` alineado a la izquierda con 8px de
  padding, y dos flechas de preset `‹ ›` en `textMid` a la derecha dentro del mismo rect.
  Nombre del preset de ejemplo: `BERTOM — CHORUS`.

### Footer (28px, fondo `inset`, borde-top hairline `faint`)
Barra de estado en una línea, todo en Mono 8.5px:

- Columna izquierda (`textDim`): `48 kHz · 128 spls · 10 spls latency · 2.4 % core`
- Columna centro (`textMid`): la línea de estado del plugin, en este caso
  `CORRECTING SIBULANCE −4.1 dB TOWARD TARGET` (si está en objetivo: `TRIANGLE LOCKED`)
- Columna derecha (`safe`): `GR 2.4 dB`

Ajustá el texto central para que sea el mensaje que corresponde al estado dibujado.

---

## 7. Layout requerido (880 × 540)

Todo posicionado en px, determinista.

| Zona | Bounds (x, y, w, h) | Contenido |
|---|---|---|
| Header | `16, 14, 848, 40` | título + subtítulo · preset · 4 botones |
| Hairline | `16, 66, 848, 1` | separador |
| `TRIANGLE` | `16, 78, 848, 224` | head 20 + display 190 + readout 150 a la derecha |
| Hairline | `16, 314, 848, 1` | separador |
| `SHAPE` | `16, 326, 246, 168` | SHINE 132 + BODY/AIR 78 |
| `CENTERS` | `274, 326, 172, 168` | 3 knobs 58 apilados + estado |
| `GLUE` | `458, 326, 196, 168` | 3 knobs 78 en fila |
| `CHAIN` | `666, 326, 178, 168` | 7 toggles en 2 columnas |
| Meters | `852, 326, 12, 168` | IN / OUT slim |
| Footer | `0, 512, 880, 28` | barra de estado |

Distribuí el espacio para que la jerarquía sea inequívoca:
**visor (44%) → SHINE → knobs secundarios → auditoría → estado.**

Si un módulo no entra en su caja, **achicá el padding o el gap**, nunca agrandes la caja ni
saques un control.

---

## 8. Estados a incluir (clases, sin animación)

- Estado por defecto = **reposo con medición válida** (SHINE 45, todos los toggles activos,
  visor mostrando la S fuera de objetivo).
- `.is-measuring` → el botón MEASURE activo + un anillo hairline de progreso bajo el header del
  visor + el readout de ventanas contando.
- `.is-not-measured` → el texto del visor en `warn`: `PRESS MEASURE TO CALIBRATE`.
- `.is-matched` → botón MATCH activo, LED `safe`, y una línea extra en el readout:
  `MATCHED TO REFERENCE · Δ −2.1 dB`.
- `.is-stage-off` → aplicado a los toggles de CHAIN.
- `.is-triangle-locked` → todas las bandas en `safe`, texto del footer `TRIANGLE LOCKED`.

**Sin animación**: es un render estático.

---

## 9. Formato de salida

- **Un solo archivo `.html` autocontenido**, sin dependencias externas salvo las 3 fuentes.
- Contenedor raíz `#opaline-pro` de **880×540 exactos**, `position: relative`, `overflow: hidden`.
- Posicioná todo con `top/left/width/height` en px. Si usás flex/grid, el resultado tiene que ser
  determinista y no depender del tamaño de fuente del sistema.
- Comentá cada bloque del CSS con su propósito.
- Al final, un bloque `:root { --token: valor; }` con **todos** los colores y medidas usados, para
  poder extraerlos automáticamente.
- **Interactividad mínima esperada**: los knobs deben poder girarse con el mouse (dibujar el arco
  de valor y el puntero) **solo si es JS trivial**; si te obliga a romper la regla de "sin
  animación" o a usar canvas, **entregá el estado estático con el arco ya dibujado** y listo. No
  inventes sliders nativos ni inputs de rango: se ven nativos y rompen la maqueta.
- Entregá además, si podés, **una segunda variante del layout** en otro archivo con la
  proporción distinta: visor a la izquierda vertical (300×380) y los 4 módulos apilados a la
  derecha. Sirve para comparar dos Stubborn Ideas antes de decidir.

---

## 10. Criterio de aceptación

Abierto en Chrome a 880×540, el archivo tiene que:

1. Parecer **el hermano mayor del Opaline free**, no otro plugin: mismo grafito, mismo ópalo,
   misma tipografía, mismo lenguaje de knobs de acero.
2. Comunicar en **3 segundos** que el free es "le doy a un botón" y el Pro es "veo lo que hace y
   lo controlo".
3. Tener el **triángulo visible y legible**: si el path objetivo no se distingue del espectro,
   el diseño falló.
4. No tener **ningún** control que no esté en la sección 6.
5. No usar ninguna técnica prohibida de la sección 1.
