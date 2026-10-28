# VTK with system FLTK

Feasibility prototype for hosting VTK in an FLTK OpenGL window. Tested on
Nobara/Fedora 44 with VTK 9.5.2 and system FLTK 1.4.5, using native Wayland and
X11. It renders a cone, forwards mouse input to VTK's trackball interactor,
and finalizes the VTK context before FLTK destroys its Wayland surface.

No VTK fork or VTK core rebuild is necessary for this adapter. VTK provides
`vtkGenericOpenGLRenderWindow` for an externally owned context, documented in
the [VTK API](https://vtk.org/doc/nightly/html/classvtkGenericOpenGLRenderWindow.html).
FLTK owns the event loop and context; VTK renders into its framebuffer, which
is blitted into FLTK's back buffer. The executable has no Qt or GTK ELF linkage.
System libdecor can still dynamically load GTK3 for Wayland window decorations.

## Build the adapter

Use the system FLTK and VTK development headers matching the installed runtime:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
env -u LD_LIBRARY_PATH ./build/vtk-fltk
```

On this machine the distro `vtk-devel` RPM would introduce Qt development
packages. Matching headers were extracted privately instead; configure with
`-DVTK_INCLUDE_DIR=/path/to/extracted/usr/include/vtk`. The CMake file links
only the required runtime modules instead of importing the distro's entire
VTK CMake target set. It defaults to installed FLTK shared libraries.

An optional first argument writes a PPM screenshot and exits after two seconds:

```sh
env -u LD_LIBRARY_PATH ./build/vtk-fltk /tmp/vtk-fltk.ppm
```

The normal DomeD environment prepends its optional FLTK SDK to
`LD_LIBRARY_PATH`. Clear that variable for the system-FLTK proof, or keep only
the unrelated bitmap-font library directory.

## Rebuilding VTK without Qt

A full VTK build can disable the Qt group and its frontend modules using the
documented [VTK module controls](https://docs.vtk.org/en/latest/build_instructions/build_settings.html):

```sh
cmake -S /path/to/VTK -B vtk-native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
  -DVTK_BUILD_TESTING=OFF -DVTK_BUILD_ALL_MODULES=OFF \
  -DVTK_GROUP_ENABLE_Qt=NO \
  -DVTK_MODULE_ENABLE_VTK_GUISupportQt=NO \
  -DVTK_MODULE_ENABLE_VTK_GUISupportQtQuick=NO \
  -DVTK_MODULE_ENABLE_VTK_RenderingQt=NO \
  -DVTK_MODULE_ENABLE_VTK_ViewsQt=NO \
  -DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES \
  -DVTK_MODULE_ENABLE_VTK_InteractionStyle=YES \
  -DVTK_MODULE_ENABLE_VTK_FiltersSources=YES
cmake --build vtk-native --parallel 2
```

This is a proposed full-build configuration, not a completed replacement RPM.
The tested adapter already works with the installed Qt-free VTK runtime modules.
Replacing distro `vtk-qt` also requires addressing its packaged Python consumers;
this prototype does not remove those packages. It does not yet implement XR
stereo presentation, accessibility, universal menus, or the complete VTK widget API.

Evidence is retained in `../artifacts/qt-gtk-next-20261028/`, including native
Wayland process maps, screenshots and logs. Tests used software OpenGL on a
private nested compositor and Xvfb; the active desktop was not restarted.
