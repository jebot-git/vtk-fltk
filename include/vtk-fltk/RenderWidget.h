// SPDX-License-Identifier: MIT
#pragma once
#include <FL/Fl_Gl_Window.H>
#include <memory>
#include <string>
class vtkGenericOpenGLRenderWindow;
class vtkGenericRenderWindowInteractor;
namespace vtk_fltk {
// FLTK owns the context, event loop and swaps. VTK owns its render pipeline.
class RenderWidget : public Fl_Gl_Window {
  struct Impl;
  std::unique_ptr<Impl> impl;
  void draw() override;
  int handle(int event) override;

public:
  RenderWidget(int x, int y, int width, int height,
               const char *label = nullptr);
  ~RenderWidget() override;
  void hide() override;
  vtkGenericOpenGLRenderWindow *render_window() const;
  vtkGenericRenderWindowInteractor *interactor() const;
  void capture(const std::string &ppm_path);
  bool capture_complete() const;
};
} // namespace vtk_fltk
