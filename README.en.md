# ForgeCAD — Linux CAD for Free / macOS CAD for Free

**Language / Lingua:** [Italiano](README.md) · English

A parametric 3D CAD application for Linux and macOS, written in **C++17** with **Qt 6**, an **OpenGL 3.3 Core** viewport, and a **custom geometry kernel** using a B-rep representation. Build solids and surfaces from sketches, edit their parameters, and rebuild models through a storyboard of modeling operations.

[![A mouse model in ForgeCAD, with surfaces, its parametric storyboard, and the revision panel](docs/images/forgecad-3d.png)](docs/images/forgecad-3d.png)

*Surface modeling, parametric history, and revisions in the ForgeCAD interface.*

The interface uses the common Fusion style and two application palettes, light and dark. It follows the system's light/dark preference while keeping the same control design across platforms. Color scheme detection uses Qt 6.5+; older backends depend on system palette notifications.

The application interface is in Italian. This project is developed with the help of artificial intelligence and is under active development. Italian menu labels are retained below so you can find the commands in the application; English explanations are provided alongside them.

## Contents

- [Gallery](#gallery)
- [Features](#features)
- [Supported formats](#supported-formats)
- [Requirements](#requirements)
- [Getting the source](#getting-the-source)
- [Installing on Linux](#installing-on-linux)
- [Installing on macOS](#installing-on-macos)
- [Optional CUDA support](#optional-cuda-support)
- [Your first model and controls](#your-first-model-and-controls)
- [Examples](#examples)
- [Tests and development](#tests-and-development)
- [Troubleshooting](#troubleshooting)
- [Limitations and license](#limitations-and-license)

## Gallery

Screenshots from the application. Click the images to view them at full resolution.

| Solid modeling | Feature preview |
|---|---|
| [![Solid mouse model with its feature history and reference planes](docs/images/forgecad-solid-modeling.png)](docs/images/forgecad-solid-modeling.png) | [![Shell feature preview showing the walls and openings of a hollow body](docs/images/forgecad-function-preview.png)](docs/images/forgecad-function-preview.png) |
| Building a body through parametric operations. | Inspecting the shell result before confirming the operation. |

| Surface construction | NURBS control point editing |
|---|---|
| [![Surface generated from U and V curve families, with the parameter panel and preview](docs/images/forgecad-surface-network.png)](docs/images/forgecad-surface-network.png) | [![NURBS control cage with a selected point and the axis manipulator](docs/images/forgecad-nurbs-poles.png)](docs/images/forgecad-nurbs-poles.png) |
| Two U/V curve families define the surface. | Moving control points using the cage and axis handles. |

[![Mouse drawing with projected views, section, dimensioned detail and isometric view](docs/images/forgecad-2d.png)](docs/images/forgecad-2d.png)

2D drawing with projected views, a section, a dimensioned detail and an isometric view.

## Features

### 2D sketches and 3D curves

- Sketches on the XY, XZ, and YZ planes, construction planes, and planar body faces.
- Lines, polylines, construction lines, circles, ellipses, and polygons with a configurable number of sides.
- Rectangles defined by two corners or by their center, with an option to draw squares.
- Straight slots defined by two end centers and their width, with constrained tangent lines and arcs; independent points support selection, dragging and dimensions, and are excluded from profiles. See [sketch tools](docs/sketch-tools.md) (Italian).
- Arcs defined by a center and endpoints, three-point arcs, and arcs tangent to lines or other arcs.
- Interpolating splines and NURBS; editing points and handles, and inserting or removing control points.
- Options to make spline curvature continuous, relax curves, and limit oscillations.
- Trimming, extending, splitting, filleting, and chamfering sketch entities.
- Offsetting entities and chains; moving, rotating, and copying.
- Linear and circular patterns and mirroring of sketch entities.
- Snapping to geometry, the grid, and the origin; midpoint and quadrant recognition.
- External references to axes, planes, edges, and curves; edge conversion and body sections in the sketch plane.
- **3D sketches** with polylines and splines defined by XYZ points in space.
- Associative extraction of 3D edges, trimming of 3D curves, and projection of curves onto surfaces.
- Helices and spirals that can be used as modeling paths.

### Dimensions and constraints

- Length, horizontal/vertical distance, angle, radius, and diameter dimensions, including dimensions relative to an axis.
- Coincident, horizontal, vertical, parallel, perpendicular, and collinear constraints.
- Tangent, equal, concentric, symmetric, midpoint, quadrant, point-on-entity, surface *pierce*, and fixed-geometry constraints.
- Constraints associated with offsets and patterns.
- A constraint panel, degrees-of-freedom reporting, and indication of fully defined sketches.
- Double-click dimension editing and graphical placement of annotations.
- Numeric fields accepting expressions: `25/2`, `3*(4+1.5)`, `sqrt(2)*10`, `360/7`. Supported operations include `+ - * / ^`, parentheses, `pi`, `sqrt`, `abs`, trigonometric functions in degrees, `exp`, `ln`, and `log`. Either a dot or a comma can be used as the decimal separator.
- Document units: millimeters, centimeters, meters, inches, and feet.

### Solid modeling

| Operation | Description |
|---|---|
| Primitives | Box, cylinder, sphere, cone, and torus |
| Extrusion | Sketch-based construction, one-sided, symmetric, or two-sided extrusion, and body cuts |
| Revolution | Geometry created by revolving a profile around an axis |
| Booleans | Union, intersection, and difference A − B |
| Sweep | A profile along a path, referencing sketches and 3D curves/edges |
| Loft | Connecting sections with guides and control of profile conditions |
| Fillet and chamfer | Edge operations, including selecting the edges of a face |
| Draft | Inclining faces |
| Shell | Creating a hollow body |
| Automatic threading | Creating a thread feature |
| Patterns and mirror | Linear, circular, and mirrored copies of solid or surface bodies |
| Transformations | Moving, rotating, and scaling bodies |
| Construction planes | References for new sketches and operations |

Loft accepts sections and guides from sketches and exact edges, including spatial geometry, with reorderable lists and associative references. Available G0/G1/G2 conditions depend on the command and geometry: contact, tangency, and curvature require compatible references.

### Surface modeling

- Extrusion of open profiles, surface Loft, and surface Sweep.
- Planar, ruled, boundary, and fill surfaces.
- Selection of edges and curves to define boundaries, sections, and guides.
- Copying and deleting faces.
- Offsetting, extending, and trimming surfaces.
- Sewing multiple surfaces; creating a solid when they enclose a valid volume.
- Adding thickness to turn surfaces into solids.
- Linear and circular patterns and mirroring of surfaces.

The **Functions → Surfaces** menu also provides extension of one free edge up to a plane or face (including curved faces), an ordered U/V curve network, per-contact G0/G1/G2 continuity in fills, and direct NURBS pole/weight editing with a draggable cage and XYZ table. Networks use 2–16 open curves per family, exactly one 3D crossing per pair, and sewn Coons cells with internal G0 continuity. They do not impose global G1/G2 continuity. The extension reference must cross the full extension within the search distance without cutting the original sheet.

Pole editing supports a single NURBS face, preserving degree, knots and UV trims including holes; extract a face from a sewn body with **Copy faces** first. Drag in the screen plane or edit XYZ/positive weights in the table (up to 4096 poles). Deltas and weight factors follow the upstream surface; a changed knot layout requires explicit reselection. Regularity checks are sampled and do not certify the absence of global self-intersections. These features support regeneration, Undo/Redo, native files and revisions; native format 54 preserves topological names for sketch supports and Pierce constraints and reads older files.

### Parametric history and documents

- A storyboard containing sketches, features, and bodies, with selection synchronized with the 3D view.
- Editing operation parameters and rebuilding models.
- Moving features in the history while respecting dependencies.
- Suppressing and reactivating features; a cursor for working at a position in the history.
- Multiple selection, renaming, visibility control, and deletion of objects and features.
- Highlighting the faces of a feature or an entire body.
- Undo/redo for document changes.
- A dependency graph through **Visualizza → Debug history / storyboard** (View → Debug history / storyboard).
- Saving parametric definitions and, optionally, computed bodies to speed up reopening.
- Exclusive locking of open documents and saving through a temporary file to protect file writes.

### 2D drawings

Projected, section, detail and auxiliary views; horizontal or vertical broken views with previews and full-model dimensions. See [broken views](docs/drawing-breaks.md) (Italian) for usage and limits. Circular, rectangular and polygonal details can be connected to their parent view by a line that follows placement changes.

### Visualization and analysis

- Rotation, panning, zooming, and fitting the scene into view.
- Wireframe, shaded, and shaded-with-edges display modes.
- Display and selection of hidden edges; topological identifiers for faces and edges.
- A section view with editable parameters.
- Configurable grid, planes, and axes, with an option to draw axes on top.
- Low, medium, or high tessellation quality; MSAA antialiasing.
- Studio, Soft, and Inspection lighting; configurable background, ambient light, and colors.
- Body transparency while sketching and customization of operation panels.
- Measurement of selected geometry.
- Geometric and mass properties: volume, area, length, centroid, inertia tensor, and principal moments, depending on the geometry type.
- Curvature, zebra, joint continuity, and curvature-comb analysis for curves and surfaces.
- Analysis of small geometric features.
- CUDA tessellation when compiled and available; a CPU path when CUDA is absent.

## Supported formats

| Format | Opening / importing | Saving / exporting |
|---|---|---|
| ForgeCAD `.prt` | Native document with parametric history | Native document |
| STEP (`.step`, `.stp`) | Geometry import | AP242, AP214, and AP203 |
| IGES (`.igs`, `.iges`) | Geometry import | IGES 5.3: solids (186) and surfaces (144) |
| STL | — | Triangle mesh |
| OBJ | — | Quadrilateral mesh, for example for Blender |

ForgeCAD `.prt` documents have an internal `FCAD` signature: the extension does not imply compatibility with documents from other CAD applications. STEP and IGES transfer geometry without recreating the original parametric storyboard. STEP/IGES exports use visible bodies.

## Requirements

| Component | Requirement |
|---|---|
| System | Linux with a graphical session, or macOS |
| Compiler | C++17, GCC or Clang; on macOS, Objective-C++ and AppKit are also required |
| CMake | 3.21 or newer |
| Qt | Qt 6 with Widgets, OpenGL, and OpenGLWidgets, including development files |
| Graphics | Drivers and a desktop OpenGL 3.3 Core context |
| OpenCASCADE / OCCT | Development files and CMake configuration, required by the current build for tests |
| CUDA | Optional, for tessellation on NVIDIA GPUs |

The application and custom kernel do not link OpenCASCADE for modeling or STEP/IGES exchange. However, the current `CMakeLists.txt` files require OCCT and configure comparison tests even when building only the `forgecad` target.

The instructions below describe building from source. Use a fresh build directory on each machine, avoiding CMake caches and binaries from another system.

## Getting the source

On GitHub, choose **Code → Download ZIP** and extract the project, or clone the repository. This is the currently configured remote; the local directory is named `Linux_CAD_forFree`:

```sh
git clone https://github.com/myungit-Vins-Prototype/pdfReader_C.git Linux_CAD_forFree
cd Linux_CAD_forFree
```

If the repository is renamed or transferred, use the URL from the **Code** button on its GitHub page.

## Installing on Linux

### 1. Dependencies

**Ubuntu / Debian**, with Qt 6 and OCCT available in the package repositories:

```sh
sudo apt update
sudo apt install build-essential cmake git qt6-base-dev libqt6opengl6-dev zlib1g-dev \
  libgl1-mesa-dev libocct-foundation-dev libocct-modeling-data-dev \
  libocct-modeling-algorithms-dev libocct-data-exchange-dev \
  libocct-visualization-dev
```

On Ubuntu, some packages are in the **Universe** repository. On Wayland, also install `qt6-wayland` if it is available for your distribution.

**Arch Linux / pacman-compatible distributions:**

```sh
sudo pacman -S --needed base-devel cmake git qt6-base opencascade mesa zlib
```

On Wayland, also install `qt6-wayland`. For other distributions, install the equivalent C++ compiler, CMake, Qt 6, OpenGL, zlib, and OCCT packages with their development files. zlib supports incremental document decompression with memory limits.

References: [Qt/OpenGL on Ubuntu](https://packages.ubuntu.com/jammy/libqt6opengl6-dev), [OCCT on Ubuntu](https://packages.ubuntu.com/search?keywords=libocct-data-exchange-dev&suite=noble), [Qt on Arch](https://archlinux.org/packages/extra/x86_64/qt6-base/), and [OCCT on Arch](https://archlinux.org/packages/extra/x86_64/opencascade/).

### 2. Build and run

From the source directory:

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target forgecad --parallel 2
./build-linux/forgecad
```

`--parallel 2` limits memory use; increase the number on machines with sufficient resources. To open a document directly:

```sh
./build-linux/forgecad "File_Esempio/Parallelepipedo.prt"
```

### 3. Desktop integration or system installation

The included script adds a menu entry, icons, and ForgeCAD `.prt` document associations for the current user, without root privileges:

```sh
sh packaging/linux/install-desktop-integration.sh "$PWD/build-linux/forgecad"
```

The shortcut points to the compiled binary: after moving the directory, run the integration again with the new path. The `xdg-mime`, `update-mime-database`, and `update-desktop-database` utilities update associations; install the corresponding distribution packages if needed.

Alternatively, install the binary and desktop integration system-wide:

```sh
sudo cmake --install build-linux --prefix /usr/local
forgecad
```

To uninstall the current user's desktop integration:

```sh
sh packaging/linux/uninstall-desktop-integration.sh
```

To uninstall the system installation using the prefix above:

```sh
sudo sh packaging/linux/uninstall-desktop-integration.sh --system /usr/local
```

Additional details are in [packaging/linux/README.md](packaging/linux/README.md) (in Italian).

## Installing on macOS

### 1. Tools and dependencies

Install Apple's command-line tools:

```sh
xcode-select --install
```

Install [Homebrew](https://brew.sh/) following its official instructions, then run:

```sh
brew install cmake qt opencascade
```

The official formulas are [Qt](https://formulae.brew.sh/formula/qt) and [OpenCASCADE](https://formulae.brew.sh/formula/opencascade). Use a compiler and libraries with the same architecture: arm64 on Apple Silicon or x86_64 on Intel.

CMake also requires zlib, which is provided by the SDK installed with the Xcode Command Line Tools.

### 2. Build and run

From the source directory:

```sh
cmake -S . -B build-macos \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix opencascade)"
cmake --build build-macos --target forgecad --parallel 2
./build-macos/forgecad
```

`brew --prefix` resolves the dependency paths for Homebrew. The macOS build includes native menu integration through AppKit and uses CPU tessellation.

To open a document:

```sh
./build-macos/forgecad "File_Esempio/Parallelepipedo.prt"
```

The current configuration produces an **executable launched from Terminal**. It does not create a standalone `.app` bundle or a `.dmg` installer; the Homebrew libraries must remain installed. The desktop integration scripts in the Linux section are intended for Linux.

## Optional CUDA support

On Linux, CMake automatically searches for a CUDA compiler and also checks `/opt/cuda/bin/nvcc`. If found, CUDA support is compiled; otherwise, the CPU path is used.

A toolkit compatible with the C++ compiler, an NVIDIA driver, and a supported GPU are required. The option is under **Opzioni → Tassellazione con CUDA** (Options → CUDA tessellation). CUDA accelerates surface evaluation/tessellation without replacing all CPU geometry modeling.

On a machine without a CUDA compiler, the regular build commands automatically produce a CPU build. The current configuration does not expose a dedicated option to disable CUDA at build time: setting `CMAKE_CUDA_COMPILER=NOTFOUND` is not sufficient when `/opt/cuda/bin/nvcc` exists, because CMake selects it again. Disabling CUDA tessellation in the interface changes the runtime path but does not remove the toolkit dependency for compilation.

## Your first model and controls

1. Launch ForgeCAD and choose **File → Nuovo** (New).
2. Set units under **Opzioni → Unità di misura del documento** (Options → Document units).
3. Create a sketch through **Schizzo → Nuovo schizzo** (Sketch → New sketch), choosing a plane.
4. Draw a rectangle or another closed profile, adding dimensions and constraints.
5. Finish sketch mode and choose **Funzioni → Estrusione** (Features → Extrusion).
6. Set the length and confirm: the body and feature appear in the storyboard.
7. Reopen the sketch or feature parameters to modify the model. Use **Modifica → Rigenera tutto** (Edit → Rebuild all) when needed.
8. Save as `.prt`, or export the geometry for other applications.

### Interaction and shortcuts

- Click to select; use `Ctrl` or `Shift` to add/remove items from the selection.
- Drag with the middle mouse button, or hold `Space` and drag with the left button, to pan. Customize the key under **Opzioni → Tasto per il pan** (Options → Pan key).
- Use the mouse wheel to zoom; `+` and `−` to zoom in or out; `0` to fit the scene.
- Double-click a sketch to edit it; use the context menu on features and bodies for parameters and visibility.
- `F2` to rename in the tree, `Delete` to delete, and `Esc` to stop the tool or close the active panel.

| Action | Linux shortcut |
|---|---|
| New / Open / Save | `Ctrl+N` / `Ctrl+O` / `Ctrl+S` |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Shift+Z` or `Ctrl+Y` |
| Import STEP/IGES | `Ctrl+I` |
| Rebuild all | `Ctrl+B` |
| Extrusion / Revolution | `Ctrl+E` / `Ctrl+R` |
| Construction plane | `Ctrl+Shift+P` |
| Mass properties / Measure | `Ctrl+M` / `Ctrl+Shift+M` |
| Show all objects | `Ctrl+Shift+H` |
| Section view | `Ctrl+Shift+X` |
| Dimension in sketch | `D` |
| Trim / Extend / Split in sketch | `T` / `E` / `S` |
| Fillet / Chamfer in sketch | `R` / `M` |

On macOS, standard shortcuts follow Qt and system conventions. Check the combinations displayed in the menus for each command.

## Examples

The [File_Esempio](File_Esempio/) directory contains `.prt` documents and STEP files:

- `Parallelepipedo.prt`: a simple starting point.
- `esempio_elica_sweep_loft.prt`: paths, Sweep, and Loft.
- `filetto.prt`: threading.
- `Loft_offset.prt`: Loft and surface offset.
- `TaglioSuperfici.prt`: working with surfaces.
- `Mouse.prt` and `mouse2_loft_corretto.prt`: more complex shapes.

Use **Salva con nome** (Save as) to experiment on a copy.

## Tests and development

The `forgecad` target builds the application. To also build and run the configured tests:

```sh
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
```

On macOS, replace `build-linux` with `build-macos`. To add viewport interaction tests:

```sh
cmake -S . -B build-linux -DFORGECAD_BUILD_VIEW_TESTS=ON
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
```

The same option can be added to the macOS configuration while retaining the Homebrew parameters. Viewport tests registered with CTest use Qt offscreen; OpenGL checks require a suitable graphical environment. CUDA tests are configured only when a CUDA compiler is available.

Tests using real documents require the example files. If they are stored outside the checkout, configure `-DFORGECAD_EXAMPLE_DIR=/path/to/examples`: CTest uses this path even when the environment variable of the same name differs. Direct runner invocations can instead use the `FORGECAD_EXAMPLE_DIR` environment variable. Tests save copies in temporary directories and preserve the originals.

The offline [interactive architecture map](docs/architecture/index.html) groups files by responsibility and shows incoming/outgoing C++ calls, includes and function references. [Generation and coverage guide](docs/architecture/README.md). Run `python3 tools/open_architecture.py` to **update the map whenever it is opened or reloaded**, then view it in your browser. The CMake build exports `compile_commands.json` without running map analysis; Python 3 and libclang bindings are only needed to update the map. Review curated responsibility descriptions after structural changes and run `python3 tools/generate_architecture.py --check`, as required by `AGENTS.md`.

| Path | Contents |
|---|---|
| `forgeCad2026.cpp` | Startup and OpenGL configuration |
| `forgeCad2026_gui.h/.cpp` | Main window; seven initialization phases for scene, menus, commands, options and toolbars |
| `forgeCad2026_viewport.h/.cpp`, `forgeCad2026_viewport_*.cpp` | Private GUI coordinator, split into document, interaction, selection, sketch, preview, rendering and scene implementations |
| `forgeCad2026_feature_preview.h/.cpp` | Snapshot-based preview key and computation; async GUI lifecycle remains in the viewport |
| `forgeCad2026_sketch_curvature.h/.cpp` | Owned curvature-comb cache and invalidation |
| `forgeCad2026_camera.h/.cpp` | Camera state and widget-independent view, projection, ray, and orbit calculations |
| `forgeCad2026_glass_compositor.h/.cpp` | OpenGL compositor for blurred panel backdrops, owning its shaders and framebuffers |
| `forgeCad2026_gpu_picker.h/.cpp` | GPU picking buffer: per-frame cache, identifier readback and owned resources |
| `forgeCad2026_scene_renderer.h/.cpp` | OpenGL scene pass: background, MSAA framebuffers, resolve, output opacity, analysis lines, axes/grid/reference planes and datum pass, 3D sections, previews, bodies and selection/topology highlights |
| `cad_display_cache.h/.cpp` | GPU cache and display rendering: faces, edges, outlines and instances |
| `forgeCad2026_gui_common.h/.cpp` | GUI helpers shared by the window and viewport |
| `forgeCad2026_storyboard.h/.cpp` | Storyboard tree, badges, and history cursor |
| `forgeCad2026_dialogs.h`, `forgeCad2026_dialogs_*.cpp` | Operation dialogs grouped into solids, surfaces, sketches/curves, and analysis |
| `forgeCad2026_dialogs_common.h/.cpp` | Shared panels, previews, and reference pickers |
| `cad_*.cpp`, `cad_*.h` | Sketches, constraints, features, history, documents, analysis, and file exchange |
| `kernel/` | Geometry kernel, B-rep, curves/surfaces, and numerical tests |
| `tests/` | Application and interaction tests |
| `packaging/` | Linux integration and macOS metadata |
| `icons/` | Icons and resources |
| `docs/` | Technical notes and performance documentation |
| `File_Esempio/` | Example models |

## Troubleshooting

**CMake cannot find Qt 6 or OpenCASCADE.** Install the development packages. For custom paths, pass `-DCMAKE_PREFIX_PATH="/prefix/qt;/prefix/occt"`, or set `Qt6_DIR` and `OpenCASCADE_DIR` to the directories containing their respective `*Config.cmake` files. On macOS, use the Homebrew configuration above.

**CMake cache from another machine or compiler.** Configure a fresh build directory.

**Insufficient memory during compilation.** Reduce parallelism to `--parallel 1`.

**CUDA is detected, but configuration or compilation fails.** Check toolkit, compiler, and GPU compatibility; if necessary, specify a supported architecture with `-DCMAKE_CUDA_ARCHITECTURES=...`. The CPU path is selected automatically on a machine without a CUDA compiler; see the CUDA section for the current detection behavior.

**Graphics problems on Wayland.** Check the Qt Wayland plugin and drivers. With XWayland available, try this for diagnosis:

```sh
QT_QPA_PLATFORM=xcb ./build-linux/forgecad
```

**Problems on a Linux laptop with Intel + NVIDIA graphics.** The application attempts to prefer NVIDIA when it detects the driver's EGL configuration. To try the integrated GPU:

```sh
FORGECAD_IGPU=1 ./build-linux/forgecad
```

**The model looks different after an update.** Use **Modifica → Rigenera tutto** (Edit → Rebuild all). Documents can contain precomputed bodies and meshes; rebuilding helps diagnose missing references and invalid features.

**A document is already open.** ForgeCAD uses a `.lock` file to prevent concurrent writes. Close the other instance using the document before editing it.

When reporting a problem on GitHub, include your operating system, architecture, Qt/CMake/OCCT versions, GPU, error message, and reproduction steps. Attach a minimal model that can be shared when possible.

## Limitations and license

Geometry operations and STEP/IGES exchange depend on the entities supported by the custom kernel; compatibility with every file or geometric configuration is not guaranteed. Installation instructions are based on the project configuration and do not certify operation on every Linux or macOS version.

The project is distributed under the **GNU General Public License v3 (GPLv3)**. The full text is in [LICENSE](LICENSE), identical to the copy on the `main` branch. Dependencies retain their respective licenses.
