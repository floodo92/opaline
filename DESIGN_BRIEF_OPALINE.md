# BRIEF — Rediseño UI Opaline (generar HTML/CSS)

Pegar todo este documento como prompt. El objetivo es producir **un único archivo HTML+CSS**
que después se porta **1:1 a C++/JUCE**. No es una maqueta libre: es una especificación visual ejecutable.

---

## 0. Objetivo

Diseñar la interfaz de **Opaline**, un plugin de audio comercial (VST3, efecto vocal).
La UI actual es plana y sin jerarquía; se busca un rediseño **comercial, profesional**, que
se vea hecho por ingenieros y se lea a 30 cm en un estudio oscuro.

El resultado debe ser **implementable en JUCE** con la API de `Graphics`.

---

## 1. REGLA DE ORO — subset implementable

Todo lo que uses tiene que poder dibujarse en JUCE: fills, gradientes lineales/radiales,
rectángulos redondeados, líneas de 1px, texto y paths vectoriales simples.

### PROHIBIDO (si lo usás, el port a JUCE queda plano y el diseño se rompe)

- `backdrop-filter`
- `filter: blur()` / `drop-shadow()`
- `mix-blend-mode`
- `conic-gradient`
- ruido o texturas (grain, `repeating-linear-gradient` como ruido)
- animaciones, `transition`, `transform` 3D / perspectiva
- **más de una capa de `box-shadow`** (una sola, blur ≤ 16px, solo para sombra de panel)
- imágenes externas, SVG, icon fonts, webfonts fuera de las 3 permitidas
- glifos/iconos que no sean texto o un path vectorial trivial

### PERMITIDO

- `background-color`, `linear-gradient` (2–4 stops), `radial-gradient` (simple)
- `border`, `border-radius`, `outline`, hairlines de 1px
- una `box-shadow` de baja difusión
- `rgba()`
- `font-family` / `font-size` / `font-weight` (400/500/600/700) / `letter-spacing` / `text-transform`
- `::before` / `::after` **solo** para líneas o rects planos

---

## 2. Canvas

- Exactamente **400 × 460 px**, fijo. No responsive, sin media queries.
- Sin scroll, sin márgenes: `body` de 400×460 exactos.
- Se va a screenshotear con Chrome headless a 1× y 2×.
- Fondo del plugin: `#060708`.

---

## 3. Sistema de diseño (usar estos tokens, NO inventar paleta nueva)

Estética declarada: **"maison de alta costura"** — grafito profundo, acento ópalo iridiscente,
hairlines finísimas, tipografía editorial, **cero glow barato**.

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
| textLo | `#5a626c` |

### Semánticos (SOLO meters y acciones peligrosas)
`safe #52c49a` · `warn #e0ad5a` · `danger #e06a6a`

### Radios
`3 / 5 / 8 / 10 px`

### Metal de knobs
`hi #a9a9b3` · `mid #75757f` · `lo #484852` — **acero, no cromo**. Nada de reflejos duros.

### Tipografía (las 3 disponibles como webfonts)
- **Inter** → UI y labels funcionales
- **Barlow Condensed** → display: títulos, MAYÚSCULAS con tracking amplio
- **JetBrains Mono** → valores numéricos (tabular)

---

## 4. Dirección estética: INSTRUMENTO DE PRECISIÓN

Con la sobriedad de superficies de una casa editorial. En concreto:

- **Alto contraste en lo que importa**: valores numéricos y meters. Son lo que más peso visual merece.
- **Superficies tranquilas**: grafito + hairlines. Sin glow, sin gradientes llamativos.
- **El ópalo es un secreto**: aparece en estado activo, en líneas finas e indicadores. Nunca como pared de color.
- **Una sola concesión de marca**: un sheen iridiscente sutilísimo en el indicador del knob principal o el logotipo. Un guiño, no un decorado.
- **Prohibido el iridiscente protagonista**: un gradiente iridiscente dominante se lee *consumer / gaming* y baja el precio percibido.
- Referencias de mercado objetivo: **FabFilter, iZotope, Sonible, TC Electronic, Valhalla**. Todos se ven calmados, legibles y caros.

---

## 5. Controles REALES — no inventar ninguno

Estos son **todos** los controles que existen. No agregues analizador de espectro, waveform,
curvas de EQ, meters adicionales, tabs, botones extra ni ninguna otra data-viz.

| # | Control | Rango | Default | Notas |
|---|---|---|---|---|
| 1 | Knob **SHINE** | 0 … 100 | 0 | unipolar. Es el control principal (hero) |
| 2 | Knob **BODY / AIR** | −100 … +100 | 0 | **bipolar**: el centro es 0 y el indicador de centro importa |
| 3 | Knob **OUTPUT** | 0 … 100 | 70 | unipolar |
| 4 | Botón **BYPASS** | on/off | off | toggle con estado visible |
| 5 | Botón **MEASURE** | — | — | acción momentánea; tiene estado activo/inactivo |
| 6 | Meter **IN** | — | — | dos filas: L y R |
| 7 | Meter **OUT** | — | — | dos filas: L y R |
| 8 | Línea de **estado** (footer) | — | — | texto de estado, fondo `inset` |

Labels exactos, en inglés: `OPALINE`, `VOCAL POLISH`, `BYPASS`, `MEASURE`, `SHINE`,
`BODY / AIR`, `OUTPUT`, `IN`, `OUT`.

---

## 6. Layout requerido (400 × 460)

- **Header** (arriba): a la izquierda la marca `OPALINE` con subtítulo `VOCAL POLISH`;
  a la derecha los botones `BYPASS` y `MEASURE`.
- **Zona central**: el knob `SHINE` como hero, grande, con su label y su valor.
- **Meters** `IN` y `OUT`, cada uno con filas L/R y su label.
- **Abajo**: knobs `BODY / AIR` y `OUTPUT` lado a lado, cada uno con label y valor.
- **Footer**: línea de estado fina sobre fondo `inset`.

Distribuí el espacio vertical para que la jerarquía sea obvia: el knob principal manda.

---

## 7. Formato de salida

- **Un solo archivo `.html` autocontenido**, sin dependencias externas salvo las 3 fuentes.
- Contenedor raíz `#opaline` de **400×460 exactos**, `position: relative`, `overflow: hidden`.
- Posicioná todo con `top/left/width/height` en px. Si usás flex/grid, el resultado tiene que ser determinista y no depender del tamaño de fuente del sistema.
- Comentá cada bloque del CSS con su propósito.
- Al final, un bloque `:root { --token: valor; }` con **todos** los colores y medidas usados, para poder extraerlos automáticamente.
- Incluí clases de estado `.is-active` y `.is-measured` con sus deltas visuales, pero el estado por defecto debe ser el de reposo.
- **Sin animación**: es un render estático.

---

## 8. Entregable

Un único archivo HTML autocontenido que, abierto en Chrome a 400×460, se vea correcto y terminado.
Se va a screenshotear con Chrome headless y comparar píxel a píxel contra el render de JUCE:
todo lo que uses debe poder replicarse con la API de `Graphics`.
