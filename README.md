# osp_renderer

OSPRay renderer for the Antarctic englacial isochrone volume produced by the
[singn](https://github.com/JixianLi/singn) pipeline. Configure camera, transfer
function and lighting interactively at low resolution on a laptop; render the
same keyframe script at high resolution on a single TACC Lonestar6 node.

Rendering is script-driven and keyframe-based: the preview logs keyframes, the
batch renderer interpolates camera and transfer function between them.

## Status

Milestone 1 of 5. What exists today:

| | |
|---|---|
| Working | CMake + `ext/` fetch, JSON keyframe script, camera spline, `scivis` render to PNG sequence, Apptainer def |
| Scene types | `tetrahedron` only -- the built-in test scene |
| Not yet built | `.vts`/`.vti` reader, volume rendering, ParaView colormap XML, interactive preview |

Milestones 2-5: Apptainer run on Lonestar6; VTK XML reader + `structuredRegular`
volume + colormaps; the ImGui preview; the unstructured-hex path for `.vts`.

## Setup

Requires only a C++17 compiler, CMake >= 3.24, and network access on first run.
Everything else is fetched into `ext/`.

```bash
./scripts/fetch_ext.sh                    # ~110 MB on macOS, ~330 MB on Linux
cmake -S . -B build -G Ninja
cmake --build build
./build/bin/ospr_render scenes/tetra.json
```

`scripts/fetch_ext.sh` pins versions in `scripts/ext.versions` and verifies
`scripts/ext.sha256`; a drifted download fails loudly rather than silently
building against something else. `--record` rewrites the manifest after a
deliberate version bump, `--force` re-downloads.

`ext/` is platform-specific: it holds the macOS arm64 OSPRay on a Mac and the
Linux x86_64 one in the container. Do not share one `ext/` across both.

## Render script

```jsonc
{
  "scene":    { "type": "tetrahedron" },
  "output":   { "dir": "out/tetra", "width": 800, "height": 600, "spp": 32,
                "background": [0.05, 0.06, 0.09] },
  "timeline": { "fps": 30, "duration": 4.0 },
  "keyframes": [
    { "t": 0.0, "ease": "smooth",
      "camera": { "position": [5, 0, 2], "target": [0, 0, 0], "up": [0, 0, 1], "fovy": 40 } }
  ]
}
```

Camera position and target follow a uniform Catmull-Rom spline through the
keyframes; field of view is interpolated linearly (a spline could overshoot past
the valid range on a sharp zoom) and `up` is blended then re-orthogonalised
against the view direction. `ease` is `smooth` (smoothstep, the default) or
`linear`, and applies to the segment leaving that keyframe.

The spline deliberately overshoots between control points -- that is what gives
a camera path its arc. Unevenly spaced keyframes give uneven speed; there is no
arc-length reparameterisation.

```
ospr_render <script.json> [--out DIR] [--frame N]
```

## Dependencies

All seven live in `ext/`, none are taken from the system.

| Library | Why |
|---|---|
| OSPRay 3.2.0 (binary release) | the renderer; bundles Embree 4, Open VKL, rkcommon, oneTBB, Open Image Denoise |
| pugixml 1.14 | VTK XML headers and ParaView colormap XML -- one dependency, two jobs |
| miniz 3.0.2 | VTK XML appended data is zlib-deflated |
| nlohmann/json 3.11.3 | keyframe script format |
| stb_image_write | PNG frame output |
| GLFW 3.4 | preview window (preview build only) |
| Dear ImGui 1.91.5 | transfer-function and lighting UI (preview build only) |

The prebuilt OSPRay release is used rather than the superbuild: building from
source pulls six subprojects plus the ISPC compiler.

**VTK is deliberately not a dependency.** It would be hundreds of megabytes and
dozens of libraries for one file reader. The VTK XML appended-data format is
well specified and only `StructuredGrid`/`ImageData` with Float32 arrays are
needed, which is a few hundred lines against pugixml and miniz.

## Constraints

- **OSPRay has no curvilinear volume type.** `structuredRegular`,
  `structuredSpherical`, `amr`, `unstructured`, `vdb` and `particle` only. The
  pipeline's `.vts` is a warped-in-Z structured grid, so it is either resampled
  to a regular grid (the `.vti` twin, the current plan) or converted to
  ~7.3M hexahedra as an `unstructured` volume.
- **No GPU rendering on Lonestar6.** OSPRay's GPU backend is SYCL/Level-Zero and
  supports Intel GPUs only, so the A100 and H100 nodes cannot be used. Request a
  plain compute node: 2x AMD EPYC 7763, 128 cores, 256 GB.
- **Frames are not bit-identical across machines.** Local is arm64/NEON, remote
  is Zen3/AVX2, and the ISA dispatch differs. Preview and final match visually
  and in framing; they will not match a checksum.
- **The build bakes an absolute RPATH** to `ext/ospray/lib`. Relocating a built
  checkout requires reconfiguring CMake.

## Lonestar6

`apptainer/osp_renderer.def` builds a toolchain-only container: it carries the
compilers and archive tools, and the source tree stays on the host and is
bind-mounted, so a code change is a rebuild rather than an image rebuild.

```bash
module load tacc-apptainer
export APPTAINER_CACHEDIR=$SCRATCH/.apptainer
apptainer build --fakeroot $SCRATCH/osp_renderer.sif apptainer/osp_renderer.def
apptainer run-help $SCRATCH/osp_renderer.sif      # full build-and-run sequence
```

The image sets `OSPRAY_NUM_THREADS` from `SLURM_CPUS_ON_NODE`, falling back to 4
so a login-node build cannot saturate the machine. Override per run with
`--osp:num-threads=N`.

## Layout

```
apptainer/   container definition for Lonestar6
build/       CMake build tree (gitignored)
cmake/       ext/ target definitions
ext/         every third-party dependency (gitignored, populated by fetch_ext.sh)
include/     public headers of the core library
lib/         static library output (gitignored)
scenes/      render scripts
scripts/     dependency fetch, version pins, checksum manifest
src/core/    libospr_core -- no windowing dependencies, links on a headless node
src/render/  ospr_render, the headless batch renderer
src/preview/ ospr_preview, the interactive preview (not yet built)
```

The core library carries no GL or GLFW dependency so that `ospr_render` links
and runs on a compute node with no display.
