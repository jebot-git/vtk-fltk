// SPDX-License-Identifier: MIT
#define GL_GLEXT_PROTOTYPES
#include <FL/Fl.H>
#include <FL/gl.h>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <vtk-fltk/RenderWidget.h>
#include <vtkAutoInit.h>
#include <vtkCallbackCommand.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkGenericRenderWindowInteractor.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkNew.h>
#include <vtkOpenGLState.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);
namespace vtk_fltk {
struct RenderWidget::Impl {
  vtkNew<vtkGenericOpenGLRenderWindow> window;
  vtkNew<vtkGenericRenderWindowInteractor> interactor;
  vtkNew<vtkInteractorStyleTrackballCamera> style;
  vtkNew<vtkCallbackCommand> context;
  bool initialized = false, captured = false;
  std::string capture_path;
};
RenderWidget::RenderWidget(int x, int y, int width, int height,
                           const char *label)
    : Fl_Gl_Window(x, y, width, height, label), impl(std::make_unique<Impl>()) {
  mode(FL_RGB | FL_DOUBLE | FL_DEPTH | FL_OPENGL3);
  end();
  impl->window->SetOwnContext(false);
  impl->window->SetReadyForRendering(true);
  impl->window->SetFrameBlitModeToNoBlit();
  impl->context->SetClientData(this);
  impl->context->SetCallback(
      [](vtkObject *, unsigned long event, void *data, void *call) {
        auto *self = static_cast<RenderWidget *>(data);
        if (event == vtkCommand::WindowMakeCurrentEvent)
          self->make_current();
        else if (event == vtkCommand::WindowIsCurrentEvent)
          *static_cast<bool *>(call) = true;
        else if (event == vtkCommand::WindowSupportsOpenGLEvent ||
                 event == vtkCommand::WindowIsDirectEvent)
          *static_cast<int *>(call) = 1;
      });
  for (auto event :
       {vtkCommand::WindowMakeCurrentEvent, vtkCommand::WindowIsCurrentEvent,
        vtkCommand::WindowSupportsOpenGLEvent, vtkCommand::WindowIsDirectEvent})
    impl->window->AddObserver(event, impl->context);
  impl->interactor->SetRenderWindow(impl->window);
  impl->interactor->SetInteractorStyle(impl->style);
  impl->interactor->EnableRenderOff();
}
RenderWidget::~RenderWidget() {
  if (shown())
    hide();
}
vtkGenericOpenGLRenderWindow *RenderWidget::render_window() const {
  return impl->window;
}
vtkGenericRenderWindowInteractor *RenderWidget::interactor() const {
  return impl->interactor;
}
void RenderWidget::hide() {
  if (impl->initialized) {
    make_current();
    impl->window->Finalize();
    impl->initialized = false;
  }
  Fl_Gl_Window::hide();
}
void RenderWidget::capture(const std::string &path) {
  impl->capture_path = path;
  impl->captured = false;
  redraw();
}
bool RenderWidget::capture_complete() const { return impl->captured; }
void RenderWidget::draw() {
  if (!impl->initialized) {
    impl->window->OpenGLInitContext();
    impl->interactor->Initialize();
    impl->initialized = true;
  }
  impl->window->GetState()->Reset();
  impl->window->SetSize(pixel_w(), pixel_h());
  impl->interactor->SetSize(pixel_w(), pixel_h());
  impl->window->Render();
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glDrawBuffer(GL_BACK);
  impl->window->BlitDisplayFramebuffer();
  if (!impl->capture_path.empty() && !impl->captured) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    std::vector<unsigned char> data(size_t(pixel_w()) * pixel_h() * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, pixel_w(), pixel_h(), GL_RGB, GL_UNSIGNED_BYTE,
                 data.data());
    std::ofstream out(impl->capture_path, std::ios::binary);
    out << "P6\n" << pixel_w() << " " << pixel_h() << "\n255\n";
    for (int y = pixel_h() - 1; y >= 0; --y)
      out.write(
          reinterpret_cast<char *>(data.data() + size_t(y) * pixel_w() * 3),
          pixel_w() * 3);
    if (!out)
      throw std::runtime_error("Cannot write VTK capture");
    impl->captured = true;
  }
}
int RenderWidget::handle(int event) {
  if (event == FL_FOCUS || event == FL_UNFOCUS)
    return 1;
  if (event == FL_PUSH || event == FL_RELEASE || event == FL_DRAG ||
      event == FL_MOVE || event == FL_MOUSEWHEEL) {
    if (event == FL_PUSH)
      take_focus();
    impl->interactor->SetEventInformationFlipY(
        int((Fl::event_x() - x()) * pixels_per_unit()),
        int((Fl::event_y() - y()) * pixels_per_unit()),
        Fl::event_state(FL_CTRL), Fl::event_state(FL_SHIFT));
    if (event == FL_PUSH || event == FL_RELEASE) {
      auto vtk_event =
          Fl::event_button() == 1
              ? (event == FL_PUSH ? vtkCommand::LeftButtonPressEvent
                                  : vtkCommand::LeftButtonReleaseEvent)
          : Fl::event_button() == 2
              ? (event == FL_PUSH ? vtkCommand::MiddleButtonPressEvent
                                  : vtkCommand::MiddleButtonReleaseEvent)
              : (event == FL_PUSH ? vtkCommand::RightButtonPressEvent
                                  : vtkCommand::RightButtonReleaseEvent);
      impl->interactor->InvokeEvent(vtk_event);
    } else if (event == FL_MOUSEWHEEL) {
      for (int i = 0; i < std::abs(Fl::event_dy()); ++i)
        impl->interactor->InvokeEvent(
            Fl::event_dy() < 0 ? vtkCommand::MouseWheelForwardEvent
                               : vtkCommand::MouseWheelBackwardEvent);
    } else
      impl->interactor->InvokeEvent(vtkCommand::MouseMoveEvent);
    redraw();
    return 1;
  }
  return Fl_Gl_Window::handle(event);
}
} // namespace vtk_fltk
