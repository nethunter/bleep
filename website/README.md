# Ble(e)p website

The static marketing site for [bleep.hml.tech](https://bleep.hml.tech), hosted
with Firebase Hosting in the `hml-studio` Google Cloud project.

## Preview locally

No install or bundler is required. Serve the directory over HTTP so the 3D
model can load its local modules and geometry:

```sh
python3 -m http.server 8765 --directory website --bind 127.0.0.1
```

For a preview with Firebase routing and headers:

```sh
cd website
firebase serve --only hosting --project hml-studio
```

## Design and interactions

The site presents Ble(e)p as an open-source DIY studio gadget. Warm ivory,
signal orange, olive, and oversized Archivo Black headlines frame a real CAD
model and an interactive studio illustration. There is no checkout or implied
finished-product availability.

- `index.html`: product story, lights/camera/audio sequence illustration,
  brightness preview, model-specific compatibility links, build CTA, and FAQ.
- `styles.css`: shared responsive marketing and compatibility design.
- `script.js`: navigation, finite user-started sequence playback, brightness
  preview, and optional model loading. Reduced motion shows the final sequence
  state immediately; each step remains individually selectable.
- `screen-tour.js`: five automatically cycling firmware-screen chapters
  (Sequences, Lighting, Cameras, Audio, Motion), illustrative tap pulses, and
  state crossfades on a 240×240 canvas texture. Pause and Next are keyboard
  accessible. Reduced motion disables autoplay; Next still selects functions.
  Hidden/offscreen/context-lost views stop the clock, and unchanged frames
  sleep between transitions. Failed tour loading retains the static screen.
- `product-model.js`: Three.js rendering from the actual top/bottom/button STL
  exports and display cover, USB-C socket, and power-switch parts extracted
  from the STEP assembly. Drag, arrow buttons, and keyboard arrows rotate the device; Home or
  Reset restores it. Render work stops offscreen and when the tab is hidden.
- `assets/device-poster.svg`: lightweight illustrated fallback for unavailable
  WebGL, failed model assets, disabled JavaScript, or Save-Data.
- `compatibility.html`: existing dated evidence ledger, restyled without
  promoting model support. Detailed repository notes remain authoritative.

The sequence and brightness interactions are marketing illustrations, not
remote hardware controls or firmware UI emulators. Camera and audio start in
order, not with frame synchronization. The model uses the real enclosure
geometry, a firmware screenshot, illustrative fasteners, and an orange button
material; it is not a promise of a manufactured finish.

## Assets and maintenance

Three.js **0.180.0** is pinned and vendored under `assets/vendor/`; its MIT
license is included. No CDN scripts or runtime package installation are needed.
Archivo Black, Manrope, and DM Mono are self-hosted as Latin WOFF2 subsets
under `assets/fonts/`, with their SIL Open Font Licenses. The site makes no
third-party runtime requests.

The top, bottom, and button STL files under `assets/models/` are byte-for-byte copies of the
matching `hardware/Bleep Remote {Top,Bottom,Button}.stl` files. When the enclosure
changes, refresh these copies together and inspect the assembled model. The
original assembly coordinates must be preserved. The LCD-MIANBAN display cover,
USB-C socket, and SS12D00 power switch are exported from the STEP assembly with
`tools/export_website_components.py` (Python 3.12, `cadquery-ocp==7.8.1.1`,
`vtk==9.3.1`). The display cover fills the 42.1 mm case opening; its rounded
shoulder rises from z=0 to the flat touch face at z=1.9 mm. The animated
32.8 mm display and its annular border replace the cover's solid front cap
at z=1.9 mm, eliminating overlapping triangles beneath the screen. Camera
clipping planes follow the device bounds to preserve mobile depth precision.
The model adds a dark socket mouth and insulating tongue to the
single-material connector CAD part to match the physical reference. Raw user
photos are not included in the repository. `ui-sequence.png` is an
existing firmware UI screenshot used as the static fallback. `assets/screens/`
contains fresh, unchanged captures from the full `ui_sim` traversal in this
checkout. These show simulator states, not physical-device recordings:

| Website frame | Simulator capture |
| --- | --- |
| `sequence-ready.png` | `24_scenes_run_ready.png` |
| `sequence-running.png` | `26_scenes_armed.png` |
| `light-cct.png` | `20e_aputure_light_cct_optimistic.png` |
| `light-rgb.png` | `20f_aputure_light_rgb.png` |
| `camera-ready.png` | `12_canon_ready.png` |
| `camera-recording.png` | `13_canon_recording.png` |
| `audio-ready.png` | `19_tascam_ready.png` |
| `audio-recording.png` | `20_tascam_recording.png` |
| `motion-keypoints.png` | `06_shark_keys.png` |
| `motion-run.png` | `07_shark_run.png` |

Rebuild `ui_sim`, run `.pio/build/ui_sim/program`, and refresh the paired
captures together when the firmware UI changes. Tap pulses are illustrative;
lighting/motion screens show separate controls and do not imply synchronized
motion inside a sequence. Existing photography and the owner's-guide
PDF remain available; a website redesign does not regenerate the manual.

Keep asset URLs versioned when changing JS/CSS: Firebase serves them with a
one-week immutable cache. Increment the HTML script/style URLs and the dynamic
model import together. When updating model dependencies or geometry, version
their import/fetch URLs as well.

## Verification

Inspect desktop and mobile layouts, the compatibility table, navigation,
keyboard rotation/reset, drag rotation, play/pause/replay, step selection,
brightness keyboard input, FAQ, reduced motion, missing-model fallback, and
JavaScript-disabled content. Check against Firebase's configured CSP as well
as the plain static server. The existing repository policy also requires the
full Montserrat `bleep` compile after code changes; no flashing from worktrees
without explicit approval.

## Deploy

```sh
cd website
firebase deploy --only hosting --project hml-studio
```

A local preview or successful build is not a production deployment.
