# VTK with system FLTK

Native VTK render widget and geometry viewer for system FLTK 1.4. Tested on
Nobara/Fedora 44 with VTK 9.5.2, on native Wayland and X11. Qt is not used.

The shared `libvtk-fltk.so.0` hosts a `vtkGenericOpenGLRenderWindow` inside an
FLTK OpenGL widget. FLTK owns the event loop, context and buffer swaps. VTK
renders into its own framebuffer, then blits into FLTK's back buffer. Context
resources are finalized before FLTK destroys the Wayland surface. This uses
VTK's documented [external context API](https://vtk.org/doc/nightly/html/classvtkGenericOpenGLRenderWindow.html).

The viewer opens VTK, VTP/VTU/VTI/VTR/VTS, STL, OBJ and PLY datasets. It supports
mouse trackball rotation/panning/zoom, wireframe, camera reset, Unicode paths,
DomeD appearance and universal-menu export. Outside DomeD a popup menu is
available. Ctrl+O opens, Ctrl+W closes, W toggles wireframe and R resets the
camera. With no file it displays a demonstration cone.

System FLTK's selected libdecor plugin can dynamically load GTK3 for Wayland
window decorations. Neither the adapter nor viewer has Qt or GTK ELF linkage.
This is a geometry viewer and embedding adapter; it does not implement XR stereo
presentation, accessibility or the complete former Qt widget API.

## Build

Dependencies are a C++20 compiler, CMake, Ninja, system FLTK 1.4 shared-devel,
OpenGL, Fontconfig, zlib, nlohmann-json, GLib/GIO, Wayland/X11 development files,
and headers matching the installed VTK runtime. Qt development packages are
not needed.

```sh
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib64
cmake --build build-native --parallel 4
env -u LD_LIBRARY_PATH ./build-native/vtk-fltk example.vtp
```

On this system, distro `vtk-devel` would introduce Qt development packages.
Matching headers were extracted privately instead; configure with
`-DVTK_INCLUDE_DIR=/path/to/extracted/usr/include/vtk`. The project links only
the required non-Qt runtime modules, rather than importing the distro's full
VTK CMake target set. The system FLTK shared libraries are the default.

DomeD normally prepends its optional FLTK SDK to `LD_LIBRARY_PATH`. Clear that
variable for direct build-tree execution, or retain only the build directory
and unrelated bitmap-font libraries. The installed launcher filters the
optional FLTK SDK automatically.

## Global installation without VTK's Qt modules

Fedora's `vtk` core RPM already has no Qt dependency. Its separate `vtk-qt`
package supplies optional Qt modules. The original `python3-vtk` imports
`vtkRenderingQt` from both `vtk.py` and `vtkmodules.all`, and consequently
requires that optional Qt library.

`packaging/build-native-packages.py` builds two local RPMs:

- `vtk-fltk-0.2.0-1.fc44.domed1`: optimized native viewer, shared adapter,
  public header, CMake target, launcher and desktop entry.
- `python3-vtk-9.5.2-12.fc44.domed1`: repackages the matching signed Fedora
  Python RPM, omitting `vtkRenderingQt` and `vtkmodules.qt`, removing their
  blanket imports/module listing, and regenerating the edited Python caches.
  It retains the optimized non-Qt native binaries byte for byte, matching the
  unchanged system core ABI. It does not claim to provide `python3-vtk-qt`.

The source RPM and binary package provenance are recorded in local artifacts.
Keeping the core unchanged preserves the existing OpenCV/OpenCascade consumers.
This is not a claim that the complete VTK core was rebuilt. No fork or core
change is required to provide FLTK embedding and eliminate the optional Qt
modules.

```sh
python3 packaging/build-native-packages.py \
  --python-rpm /path/to/python3-vtk-9.5.2-12.fc44.x86_64.rpm \
  --work /absolute/path/to/new-package-work
# Inspect and test the resulting RPMs/stages before privileged installation.
sudo -A dnf install /path/to/python3-vtk-9.5.2-12.fc44.domed1.x86_64.rpm \
                    /path/to/vtk-fltk-0.2.0-1.fc44.domed1.x86_64.rpm
sudo -A dnf --setopt=clean_requirements_on_remove=False remove vtk-qt
```

The viewer is `/usr/bin/vtk-fltk`; the library is under `/usr/lib64`. No running
session is restarted and MIME defaults are not changed. Future distro Python
VTK upgrades must retain this Qt-free packaging policy.

## Embedding

```cmake
find_package(VTKFLTK CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE VTKFLTK::vtk-fltk-adapter)
```

Include `<vtk-fltk/RenderWidget.h>`, add a renderer to `render_window()`, and
use FLTK's normal loop. `interactor()` exposes VTK interaction. Schedule
rendering with the widget's `redraw()`. The widget owns context teardown;
close it before destroying a containing native window/context.

## Validation

```sh
python3 tests/smoke.py --binary build-native/vtk-fltk --artifacts /tmp/vtk-fltk-ui
python3 tests/wayland.py --binary build-native/vtk-fltk --artifacts /tmp/vtk-fltk-wayland
```

The Xvfb/D-Bus test independently writes five mesh formats using VTK Python,
opens Unicode filenames, checks nonuniform rendered pixels, process maps,
universal-menu commands, wireframe, mouse camera drag and clean close. Invalid
geometry is rejected. The Wayland test uses a private nested DomeD compositor;
it requires the installed `/usr/local/bin/dome-compositor`. It does not touch
the active display session. Both use software OpenGL, not hardware/XR evidence.
An external CMake consumer also compiles and links the staged public SDK.

QA options are `--capture PPM`, `--report JSON` and `--quit-after-ms N`.
Local evidence is under `../artifacts/vtk-native-20261028/`.

For an independent full VTK rebuild, the documented
[module controls](https://docs.vtk.org/en/latest/build_instructions/build_settings.html)
allow `VTK_GROUP_ENABLE_Qt=NO` and `VTK_MODULE_ENABLE_VTK_GUISupportQt=NO`,
`VTK_MODULE_ENABLE_VTK_GUISupportQtQuick=NO`,
`VTK_MODULE_ENABLE_VTK_RenderingQt=NO`, and
`VTK_MODULE_ENABLE_VTK_ViewsQt=NO`. This optional build route is not needed for
the installed core used here.
