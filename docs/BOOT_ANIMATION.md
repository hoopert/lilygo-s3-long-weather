# Boot animation: design constraints

> **Delivered.** The design pass answered this brief with the boot screen of
> `design/SPEC.md` §6 (rivet rows, a rotating quarter-arc, a status line that
> steps through the connection), now shipped in `firmware/src/ui/startup.cpp`.
> The constraints below still hold for anyone who wants to replace it with
> something more elaborate.

The brief for whoever designs the boot sequence - a person, or Claude Design.
Everything here is measured from the firmware as it runs today; nothing is
aspirational. Where a number is a budget rather than a hard limit it says so.

## The canvas

| | |
|---|---|
| Logical size | **640 × 180 px**, landscape. Design at 1:1; the driver handles the panel's physical orientation. |
| Physical size | 3.4" diagonal strip, roughly 83 × 23 mm active area, ~195 ppi. A 1 px line is visible; 12 px text is the floor for reading at arm's length; 16 px+ for anything meant to be read across a trailer. |
| Colour | **RGB565** - 5 bits red, 6 green, 5 blue. Smooth gradients band visibly (32 or 64 steps per channel, no dithering). Use flat colour, hard-edged shapes, or short gradients across a few tens of pixels. Antialiasing on edges and text is on and looks fine. |
| Ground | The UI's background is `COL_GROUND` `#0E1113` (near-black, slightly cool). An animation that ends on that colour hands off to the Today screen with no cut. |
| Palette | The theme: `#171B1E` surface, `#22282C` surface-hi, `#2E353A` rivet, `#C9D1D6` aluminium, `#7C878E` aluminium-dim, `#E8DCC8` oat, `#3FBFB0` turquoise, `#E2703A` sunset, `#6FA8C7` sky. Off-palette colour is allowed in the animation, but the last frame should live in the palette. |
| Type | Jost (Futura lineage) in the cuts the firmware already carries: hero 72, title 30, hour 24, body 20, label 15, micro 12. Material Symbols Rounded icons at 56 / 20 / 16. **Any other size or face is a new bitmap font in flash** - fine, but it is a build step, not a design-time choice. |
| Safe area | 10 px inset from every edge (`LAYOUT_SAFE`). The glass has a bezel; nothing critical in the outer 10 px. |

## Motion

| | |
|---|---|
| Frame rate | LVGL refreshes at **30 fps** (33 ms period). Every frame is a whole-screen redraw: ~10 ms for LVGL to render 640×180, ~25 ms to rotate and stream to the glass. **Simple scenes hold 30 fps; a frame with many overlapping alpha layers or a full-screen transformed image drops toward 15-20.** Animations are time-based (`lv_anim`), so a dropped frame costs smoothness, never correctness. |
| Duration | Power-on to first frame is ~1.2 s (panel init, Wi-Fi start). With saved Wi-Fi the first forecast lands **5-10 s** after power-on; on a fresh unit it never does, and the setup screen must take over. So: an **intro of 1.5-2.5 s**, then a **loopable hold** that can run indefinitely, then a **≤400 ms exit** into whichever screen is ready. The hold is the part most people will see most of. |
| Skippable | A tap during the animation should jump to the exit. Wall-mounted, nobody wants to wait for a logo twice. |
| Backlight | Comes up dark and fades to the ambient level over the first frames; the animation can lean on that (fade from black is free) or fight it (a bright first frame will be dimmed). Assume 0 → full over ~1.5 s from the first frame. |
| Easing | `lv_anim` provides linear, ease-in, ease-out, ease-in-out, overshoot, bounce, step. Choreograph with `lv_anim_timeline`. Nothing here needs code from the designer - name the curve and the timing and it maps 1:1. |
| Tearing | The panel has no vertical sync we use. A full-frame write takes ~14 ms; fast horizontal motion of high-contrast edges can show a seam for one frame. Prefer vertical motion, fades, scale and reveal over fast horizontal sweeps. |

## What the renderer can do cheaply

Cheap (30 fps, use freely):

- Move, resize, fade (opacity) any object.
- Filled rectangles with radius, borders, and shadows (`LV_DRAW_COMPLEX` is on).
- Lines, polylines, arcs (arc sweep animation is a native property - a rising sun, a gauge, a ring).
- Text: fade, move, per-letter reveal by animating a label's width clip.
- Small images (< 100 × 100) moving, fading, or scaling.
- A masked reveal: a rectangle whose width or height animates over a static composition.

Expensive (budget one per frame, or accept ~15 fps):

- Rotating or scaling an image larger than ~150 px on a side (software transform).
- Two or more full-screen layers alpha-blended together.
- A `lv_canvas` repainted every frame.
- Animated GIF decode of a large frame (not enabled today; can be, for a small logo).

Not available:

- Video, JPEG/PNG decode at runtime, shaders, 3D, blur, real gradients.

## Assets and memory

| | |
|---|---|
| Flash for assets | ~**5 MB** free in the app partition. A full-screen 640×180 RGB565 image is 230 KB; with alpha, 345 KB. A frame-by-frame sequence is therefore possible only for small sprites or a handful of full frames - **20 full frames is the ceiling**, and that is a 1-second clip. Design the animation as a composition of a few static assets moved by code, not as a filmstrip. |
| RAM | 8 MB PSRAM (the frame buffer uses 230 KB). LVGL's own working pool is 64 KB internal SRAM; a scene of a few dozen objects fits easily, a thousand particles does not. |
| Asset formats | PNG or SVG in, converted to LVGL C arrays (`lv_img_conv`) at build time. Indexed-colour PNGs (≤256 colours) are 4× smaller in flash than true-colour and look identical on this glass. |
| Where they go | `firmware/src/ui/assets/` (new), one `.c` per image, declared in one header. |

## Hand-off contract

The animation is a screen like any other (`docs/ARCHITECTURE.md`, "Adding a screen") and owns the display until one of these, whichever first:

1. A forecast is ready → exit into **Today**.
2. Wi-Fi is unconfigured or the portal is up → exit into **Setup**.
3. A tap → exit into whichever of the above is closer; Setup if neither.

Exit is the animation's responsibility (its final ≤400 ms), so it should end on `COL_GROUND` or on a composition the Today screen can crossfade from.

## Deliverables

- A storyboard: intro / hold / exit, each with timing, easing, and which elements move.
- Static assets as PNG (or SVG) at 1:1, with the palette above.
- Any new type sizes called out explicitly (each is a font build).
- A one-line statement of what the hold loop does when it has run 30 seconds - that is the case on a panel with a slow router.
