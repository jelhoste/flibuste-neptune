# Facade image style specification (`.neptunestyle` format)

Format version: **1**. This document is written to be read by a human **or handed as-is to an AI** (ChatGPT, Claude, etc.) so that it can create styles. Section 12 contains a ready-to-paste message.

## 1. In ten lines

- Neptune's **Facade** is a custom interface page: you place widgets (knobs, buttons, sliders...) bound to instrument parameters.
- A **style** is a set of images that changes a widget's *appearance*. It never changes its *function* (values, notches, direction: those are set on the widget).
- A **pack** (a `.neptunestyle` file) is a **zip** archive holding one or several styles.
- Image formats: **SVG** (recommended), **PNG**, **JPG**.
- Each image has a **role**, given by its **file name**: `off`, `on`, `track`, `handle`, `strip_N`.
- Buttons and actions: `off` + `on`. Sliders: `track` + `handle`. Knobs: `strip_N` (a strip of N pictures).
- A missing image = Neptune keeps its default drawing for that part.
- No code is executed: a pack only holds images and JSON text.

## 2. Vocabulary

| Term | Meaning |
|---|---|
| Facade | The custom interface page of a Neptune track. |
| Widget | An element placed on the Facade: knob, button, action, slider, menu, zone, keyboard... |
| Style | A set of images (one per role) dressing a widget. |
| Pack | A `.neptunestyle` file (or a folder with the same structure) holding styles. |
| Library | The folder where Neptune keeps installed packs. |

## 3. Which widgets use which images

| Widget | Images used | Notes |
|---|---|---|
| Button, Action | `off` and `on` | Two states only (off/closed, on/open). No hover or pressed image. |
| Slider (drawbar) | `track` (rail) and `handle` | Horizontal or vertical, chosen by the user. |
| Knob | `strip_N` | A strip of N pictures, one per position. |
| Menu, Zone, Label, Keyboard, Curve | none | These widgets take no style. |

One style may hold every image: each widget only uses the ones that concern it.

## 4. Pack contents

A pack is a zip archive renamed to `.neptunestyle`. The **file name** (without extension, lower case) is the pack's **identifier**: `organ-classic.neptunestyle` → `organ-classic`. Do not rename it once in use: Facades that use it find it by this name.

### 4.1 Layout

```
organ-classic.neptunestyle        (zip)
├── pack.json                     optional: description of the pack
├── drawbar-tab/                  one folder = one style
│   ├── style.json                optional: name, etc.
│   ├── off.svg
│   └── on.svg
├── slider-horizontal/
│   ├── track.svg
│   └── handle.svg
└── knob/
    └── strip_32.svg
```

- **One folder = one style.** Any folder holding at least one recognised image is a style.
- Images placed at the **root** of the archive form a single style named after the pack.
- Deeper sub-folders are accepted (each counts as a style). Folders without images are ignored.
- At most **64 styles** per pack.
- Unknown files (`README.txt`, `.DS_Store`, `__MACOSX/`, etc.) are ignored.
- Paths must not contain `..`, a backslash `\` or a drive letter.
- A plain (unzipped) folder with the same structure placed in the library is also accepted: handy while working.

### 4.2 Recognised file names

Case is ignored. Extensions: `.svg`, `.png`, `.jpg`, `.jpeg`.

| Role | Accepted names | Used by |
|---|---|---|
| off / closed | `off`, `closed`, `released` | Button, Action |
| on / open | `on`, `open`, `pressed` | Button, Action |
| rail | `track`, `rail`, `groove` | Slider |
| handle | `handle`, `thumb` | Slider |
| knob strip | `strip_N`, `strip_N_h`, `filmstrip_N`, `filmstrip_N_h` | Knob |

For the strip, **N is the number of pictures** (2 to 512): `strip_32.svg` = 32 pictures stacked **vertically**; `strip_32_h.svg` = 32 pictures **side by side**. If N is not in the name, give it in `style.json` (key `frames`), otherwise the strip is ignored with a warning.

If a role exists in several formats in the same folder (`off.svg` and `off.png`), **SVG is preferred**, then PNG, then JPG.

### 4.3 `pack.json` (optional)

```json
{
  "format": 1,
  "name": "Classic organ",
  "author": "Your name",
  "licence": "CC BY 4.0",
  "version": "1.2",
  "description": "Drawbars, sliders and knobs for an organ console."
}
```

All fields are optional. Without `name`, the pack is named after its file. `licence` may also be written `license`. Always give the author and the licence if you share the pack.

### 4.4 `style.json` (optional, in the style's folder)

```json
{
  "name": "Drawbar tab",
  "frames": 32,
  "strip_direction": "vertical"
}
```

- `name`: displayed name of the style (otherwise the folder name).
- `frames`: number of pictures of the strip (takes priority over the file name).
- `strip_direction`: `"vertical"` (stacked, default) or `"horizontal"` (side by side). Takes priority over `_h`.

## 5. Sizes and proportions

Neptune **fits** images to the widget's size; the values below are *reference proportions*, not imposed sizes. Draw large (SVG scales without loss).

| Role | How Neptune places the image | Advice |
|---|---|---|
| `off`, `on` | Centred in the widget, **proportions kept**. | Both images must have **exactly the same size and proportions**. Example: 120 × 40 (3:1). |
| `track` | **Stretched** over the whole widget (width and height). | Horizontal slider: about 300 × 24 (wide and low). Vertical: 24 × 300. Draw the rail so a slight stretch does not distort it (simple rounded ends). |
| `handle` | **Horizontal** slider: height = the slider's height, width from the image's proportions. **Vertical** slider: width = the slider's width, height from the proportions. Centred on its position. | Horizontal: tall narrow handle (e.g. 24 × 48). Vertical: wide low handle (e.g. 48 × 24). The handle never exceeds half the travel. |
| `strip_N` | One picture of the strip is **centred** in the widget, proportions kept. | **Square** pictures for a round knob (e.g. 64 × 64). |

General advice: **transparent** background around the drawing; no large empty margins (they reduce the visible part); one or two percent of margin is enough for thick outlines.

## 6. The knob strip

- The strip holds **N pictures of identical size**, from the knob's **minimum** to its **maximum** position.
- Vertical (`strip_N`): picture 1 is **at the top**, picture N **at the bottom**. Total height = N × one picture's height; width = one picture's width.
- Horizontal (`strip_N_h`): picture 1 is **on the left**, picture N **on the right**.
- Neptune shows picture number `round(position × (N − 1))` (counting from 0), where `position` runs from 0 (minimum) to 1 (maximum). With custom notches the position follows the notch.
- For a classic knob, turn the pointer from **−135° to +135°** (270° of travel, 0° = pointing up). The larger N, the smoother the motion; 32 to 64 pictures are enough.
- Pictures must *overlap exactly* (same centre of rotation) so the knob does not "wobble".
- In SVG the total height is N × one picture's height: for example `viewBox="0 0 64 2048"` for 32 pictures of 64 × 64. Place each picture in **absolute** coordinates (avoid relying on nested `translate`).
- It is **strongly advised to have a small script generate the strip** (see the example in section 11) rather than draw it by hand.

## 7. SVG: what works

Neptune displays SVG with the **JUCE** engine, which only implements a **subset** of SVG. Stay within this list; always test in the *Styles... → preview* window.

**Safe:**
- `<svg>` with `width`, `height` and `viewBox`; `<g>`; `<defs>`.
- Shapes: `<rect>` (with `rx`, `ry`), `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, `<path>` (all path commands).
- Fill and stroke: `fill`, `stroke`, `stroke-width`, `stroke-linecap`, `stroke-linejoin`, `opacity`, `fill-opacity`, `stroke-opacity`.
- Simple linear and radial gradients (`<linearGradient>`, `<radialGradient>` with `<stop>`).
- Transforms: `translate`, `scale`, `rotate`, `matrix`.
- Colours as `#rrggbb`, `#rgb`, `rgb(...)` or colour names.

**Avoid (may render wrongly or not at all):**
- Filters (`<filter>`, blur, drop shadows), masks (`<mask>`), patterns (`<pattern>`), blend modes (`mix-blend-mode`).
- CSS style sheets (`<style>`) and CSS variables: put colours directly in attributes.
- Text (`<text>`): the font depends on the machine. **Convert text to outlines** before saving.
- Embedded images (`<image>`), animations, scripts.
- Huge files: aim for less than 500 KB per image.

Tip: in Inkscape, save as "Plain SVG", after *Object → Object to Path* for texts and *Path → Stroke to Path* for complex shapes.

## 8. PNG and JPG

- **PNG**: keeps transparency (ideal for a drawbar laid over a console background). Aim for at least 2× the display size to stay sharp.
- **JPG**: no transparency. Useful for photos.
- A PNG knob strip: same rules as SVG (same-size pictures, stacked or side by side).
- Reasonable weight: less than 2 MB per image.

## 9. Installing a pack

In Neptune, **Facade tab → Edit layout → Styles... → Library... → Install a pack...**, or **drop the `.neptunestyle` file onto the Facade window**. The file is copied into the library:

- Linux: `~/.config/Neptune/styles/`
- Windows: `%APPDATA%\Neptune\styles\`
- macOS: `~/Library/Application Support/Neptune/styles/`

*Open the packs folder* opens it. Then **Library → *(pack name)* → *(style name)*** adds the style to the current Facade; give it to widgets with **Style...**.

Packs are **read inside the archive**: nothing to unzip. A style added to a Facade keeps a *reference* to the pack (`pack:<identifier>!<path>`): if the pack disappears or is renamed, the widget falls back to its default drawing.

To share a style you built by hand, use **Library... → Export the selected style...**.

## 10. Checking a pack (checklist)

1. The file ends in `.neptunestyle` and opens as a zip.
2. Each style is in a folder (or at the root), with file names from section 4.2.
3. `off` and `on` have **the same size** and proportions.
4. SVGs contain no text, filter, CSS or embedded image.
5. A knob strip states its number of pictures (`strip_32.svg`) and its height is an exact multiple of that number (width for `_h`).
6. In Neptune: *Styles...* → the preview shows the button, the knob and the sliders; try them with the mouse.

**Possible warnings when installing:**

| Message | Cause |
|---|---|
| "No style found" | No file has a recognised name. Check the names (section 4.2). |
| "a knob strip needs its number of frames" | `strip.svg` without a number: rename to `strip_32.svg` or add `frames` to `style.json`. |
| "several files for the same image" | Two formats of the same role in a folder: SVG is kept. |
| "Entry ignored (unsafe path)" | A path contains `..`, `\` or a drive letter. |
| "newer format version" | The pack announces `"format"` above 1: update Neptune. |

## 11. Complete examples

### 11.1 Drawbar tab, closed and open (`drawbar-tab/off.svg`, `drawbar-tab/on.svg`)

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="120" height="40" viewBox="0 0 120 40">
  <rect x="2" y="2" width="116" height="36" rx="6" fill="#d9d2bd" stroke="#3a3a3a" stroke-width="2"/>
  <rect x="8" y="8" width="104" height="24" rx="3" fill="#b9b19a"/>
  <line x1="20" y1="20" x2="100" y2="20" stroke="#6a6450" stroke-width="3"/>
</svg>
```

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="120" height="40" viewBox="0 0 120 40">
  <rect x="6" y="6" width="112" height="32" rx="6" fill="#5a5546"/>
  <rect x="2" y="2" width="112" height="32" rx="6" fill="#f1ead6" stroke="#3a3a3a" stroke-width="2"/>
  <rect x="8" y="8" width="100" height="20" rx="3" fill="#d9d2bd"/>
  <line x1="20" y1="18" x2="96" y2="18" stroke="#b3261e" stroke-width="4"/>
</svg>
```

### 11.2 Horizontal slider (`slider-h/track.svg` 300 × 24, `slider-h/handle.svg` 24 × 48)

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="300" height="24" viewBox="0 0 300 24">
  <rect x="0" y="0" width="300" height="24" rx="12" fill="#2a2a2a"/>
  <rect x="4" y="8" width="292" height="8" rx="4" fill="#0e0e0e"/>
</svg>
```

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="48" viewBox="0 0 24 48">
  <rect x="1" y="1" width="22" height="46" rx="5" fill="#c8c0a8" stroke="#333333" stroke-width="2"/>
  <line x1="6" y1="24" x2="18" y2="24" stroke="#555555" stroke-width="2"/>
</svg>
```

### 11.3 Python script generating a knob strip (`knob/strip_32.svg`)

```python
import math
N, SIZE = 32, 64                      # 32 pictures of 64 x 64, stacked vertically
out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{SIZE}" height="{N*SIZE}" viewBox="0 0 {SIZE} {N*SIZE}">']
for i in range(N):
    a = math.radians(-135 + 270 * i / (N - 1))   # from -135 to +135 degrees, 0 = up
    cy = i * SIZE + SIZE / 2
    x2, y2 = SIZE / 2 + 22 * math.sin(a), cy - 22 * math.cos(a)
    out.append(f'  <circle cx="{SIZE/2}" cy="{cy}" r="28" fill="#3a3a3a" stroke="#9a9a9a" stroke-width="2"/>')
    out.append(f'  <line x1="{SIZE/2}" y1="{cy}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="#f0b429" stroke-width="4" stroke-linecap="round"/>')
out.append('</svg>')
open('strip_32.svg', 'w').write('\n'.join(out))
```

### 11.4 `pack.json`

```json
{ "format": 1, "name": "My pack", "author": "Me", "licence": "CC BY 4.0", "version": "1.0" }
```

## 12. Message ready to paste into an AI

> You will create an **image style pack for the Facade of the Neptune plugin**. The format is described in the specification below: follow it strictly.
>
> **What I want:** *(describe the theme here: for example "a liturgical organ console in dark wood, ivory drawbars with a gold ring, brass knobs")*. I want these styles: *(for example: a closed/open drawbar; a horizontal slider with rail and handle; a 48-frame knob)*.
>
> **Constraints:** SVG only, no text (convert to outlines), no filters, no CSS, no embedded images, colours in `fill`/`stroke` attributes. Transparent background. `off` and `on` the same size. For the knob, write a **Python script** that generates `strip_N.svg`. Answer with: (1) the exact tree of the pack, (2) the full content of each file, (3) the script if any, (4) the command to create the zip (`zip -r my-pack.neptunestyle .`, from inside the pack folder).
>
> *(Paste the complete specification here.)*

## 13. Known limits of format 1

- Buttons: two states only. Sliders: the handle is a single image. Knobs: a strip of pictures (no two-layer knob yet).
- No nine-slice: rails are stretched as they are.
- Pack images are not yet copied into presets or into a Facade's portable ZIP; the project keeps the reference to the pack.
- The pack's file name is its identifier: renaming it breaks references.
