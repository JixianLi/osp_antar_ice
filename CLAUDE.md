# Claude Code guidance

OSPRay renderer for the singn Antarctic englacial isochrone volume. Read
[README.md](README.md) first for the pipeline, the script format, the dependency
rationale and the hard constraints.

This repository is a git submodule of `singn`; the local checkout is named
`osp_renderer`, the remote is `osp_antar_ice`.

## Layout

- `src/core/` builds `ospr_core`, which **must never gain a windowing or OpenGL
  dependency** -- `ospr_render` links it on a headless compute node. GLFW and
  ImGui belong to `src/preview/` alone.
- `include/ospr/` holds the public headers; `src/core/` holds implementations.
  Header-only is fine for pure data types (`camera.h`, `math.h`); do not create
  an empty `.cpp` to match a header.
- `cmake/ospr_ext.cmake` is the only place `ext/` paths are named.

## ext/ policy

Every third-party dependency lives in `ext/` and is fetched, never committed and
never taken from the system. Adding one means, in order:

1. add the pinned version to `scripts/ext.versions`
2. add the download and unpack to `scripts/fetch_ext.sh`
3. re-record with `./scripts/fetch_ext.sh --record`
4. define its target in `cmake/ospr_ext.cmake`, with `SYSTEM` on the include
   directory so its warnings stay out of our build

`find_package` must always pass `NO_DEFAULT_PATH`. A build that succeeds because
a system Homebrew or `/usr/lib` copy was picked up is a bug: it will not
reproduce in the container.

`ext/` is platform-specific -- `fetch_ext.sh` branches on `uname -s`/`uname -m`
to select the OSPRay binary release. Never share one `ext/` between the Mac and
the Linux container.

## Naming conventions

| Thing | Convention | Example |
|---|---|---|
| Type | `PascalCase` | `FrameRenderer`, `Keyframe` |
| Function, variable, member | `snake_case` | `camera_at`, `frame_count` |
| Private data member | trailing underscore | `width_`, `samples_per_pixel_` |
| Constant | `UPPER_SNAKE` | `OSPR_EXT` |
| File | `snake_case.cpp` / `.h` | `keyframe.cpp` |
| CMake target | `ospr_<name>` | `ospr_core`, `ospr_pugixml` |
| Namespace | `ospr` | |

Write names out. `samples_per_pixel`, not `spp`; `frame_index`, not `i`. The
exception is mathematical code, where single letters are allowed **only** with
the formulation documented alongside -- see `catmull_rom` in `keyframe.cpp` for
the expected level of detail.

JSON script keys are the one place short forms are deliberate (`spp`, `fovy`,
`dir`, `t`), because they mirror what a user types by hand.

## Code style

- No emojis in code or docs.
- Comments carry only what the code cannot: a non-obvious *why*, a hidden
  constraint, the source of a formula. Never restate the code.
- Errors are `std::runtime_error` with a message naming the file or field at
  fault; `main` catches and prints. No error codes, no silent failure.
- `#include` order: own header, standard library, `ext/` libraries, `ospr/`.

## Pitfalls

- **The OSPRay binary release ships `librkcommon` but not its headers.** There
  is no `rkcommon::math`. `include/ospr/math.h` defines our vector types and
  registers them with `OSPTYPEFOR_SPECIALIZATION`, which is what makes
  `setParam` and `CopiedData` accept them. A new vector type needs that
  registration or it fails to compile with an unhelpful message.
- **The OSPRay framebuffer origin is the lower-left corner.** `write_png_rgba`
  flips vertically; anything else consuming a mapped framebuffer must too.
- **OSPRay has no curvilinear volume type**, so a `.vts` cannot be handed to it
  directly. See README.
- **OSPRay's GPU backend is Intel-only** (SYCL/Level-Zero). Lonestar6's NVIDIA
  nodes are useless to us; this is CPU rendering on a plain compute node.
- **`CMAKE_BUILD_RPATH` is an absolute path** to `ext/ospray/lib`, because
  OSPRay dlopens `libospray_module_cpu` at device-init time. Moving a built
  checkout breaks it until CMake is reconfigured.
- **Frames are not bit-identical across arm64 and x86_64.** Verified, not
  assumed. Never diff PNGs between the local preview and a Lonestar render.
- **Rocky 9 needs the CRB repo for `ninja-build`.** Without it the whole `dnf`
  transaction fails and nothing installs.
- **`apptainer build --fakeroot` fails on any rpm whose scriptlet needs real
  root.** Two have already bitten: `dnf -y update` pulls `filesystem` and
  `util-linux` (chown, setcap), and `git` hard-depends on `openssh` (creates the
  sshd user). Neither is needed -- clone on the host, `fetch_ext.sh` uses only
  curl. Before adding any package to `%post`, predict the failure instead of
  discovering it on Lonestar:

  ```bash
  dnf download --resolve --alldeps --setopt=install_weak_deps=False <pkgs>
  for f in *.rpm; do
      rpm -qp --scripts "$f" | grep -qE "useradd|groupadd|setcap|systemd-sysusers|setfacl|mknod" \
          && rpm -qp --qf '%{NAME}\n' "$f"
  done
  ```

  Cross-check any hit against the real transaction (`dnf --assumeno install`) --
  `--alldeps` also downloads packages already present in the base image, whose
  scriptlets never run.

## Common commands

```bash
./scripts/fetch_ext.sh                     # populate ext/; idempotent
./scripts/fetch_ext.sh --record            # after a deliberate version bump
cmake -S . -B build -G Ninja && cmake --build build
./build/bin/ospr_render scenes/tetra.json
./build/bin/ospr_render scenes/tetra.json --frame 0 --out /tmp/check
```

Verifying a change against the Lonestar6 target without Lonestar access. This
catches RPATH, toolchain and fetch-script problems a Mac build cannot:

```bash
./scripts/check_linux_build.sh             # needs Docker; ~5 min under emulation
```

**Never hand-copy the def's `%post` into an ad-hoc test.** That is exactly how a
broken `dnf -y update` shipped: the def had a line the test did not.
`check_linux_build.sh` extracts `%post` and the base image from
`apptainer/osp_renderer.def` so the two cannot diverge. Change the def, re-run
the script.

It does not reproduce apptainer's `--fakeroot` user namespace, so rpm scriptlet
failures still only surface on a real `apptainer build`.
