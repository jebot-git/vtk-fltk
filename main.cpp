// SPDX-License-Identifier: MIT
#define GL_GLEXT_PROTOTYPES
#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/gl.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <vtkActor.h>
#include <vtkAutoInit.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkConeSource.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkGenericRenderWindowInteractor.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkNew.h>
#include <vtkOpenGLState.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);
class Window : public Fl_Gl_Window {
  vtkNew<vtkGenericOpenGLRenderWindow> renderWindow;
  vtkNew<vtkRenderer> renderer;
  vtkNew<vtkGenericRenderWindowInteractor> interactor;
  vtkNew<vtkInteractorStyleTrackballCamera> style;
  vtkNew<vtkCallbackCommand> callback;
  bool initialized = false, captured = false;

public:
  std::string capture;
  Window() : Fl_Gl_Window(640, 480, "VTK with system FLTK") {
    mode(FL_RGB | FL_DOUBLE | FL_DEPTH | FL_OPENGL3);
    end();
    resizable(this);
    renderWindow->SetOwnContext(false);
    renderWindow->SetReadyForRendering(true);
    renderWindow->SetSwapBuffers(true);
    renderWindow->SetFrameBlitModeToNoBlit();
    callback->SetClientData(this);
    callback->SetCallback(
        [](vtkObject *, unsigned long event, void *data, void *call) {
          auto *self = static_cast<Window *>(data);
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
          vtkCommand::WindowSupportsOpenGLEvent,
          vtkCommand::WindowIsDirectEvent})
      renderWindow->AddObserver(event, callback);
    vtkNew<vtkConeSource> cone;
    cone->SetResolution(32);
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(cone->GetOutputPort());
    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(.95, .65, .15);
    renderer->AddActor(actor);
    renderer->SetBackground(.08, .11, .16);
    renderer->ResetCamera();
    renderWindow->AddRenderer(renderer);
    interactor->SetRenderWindow(renderWindow);
    interactor->SetInteractorStyle(style);
    interactor->EnableRenderOff();
  }
  ~Window() {
    if (shown())
      hide();
  }
  void hide() override {
    if (initialized) {
      make_current();
      renderWindow->Finalize();
      initialized = false;
    }
    Fl_Gl_Window::hide();
  }
  void draw() override {
    if (!initialized) {
      renderWindow->OpenGLInitContext();
      interactor->Initialize();
      initialized = true;
      std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;
    }
    renderWindow->GetState()->Reset();
    renderWindow->SetSize(pixel_w(), pixel_h());
    interactor->SetSize(pixel_w(), pixel_h());
    renderWindow->Render();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    renderWindow->BlitDisplayFramebuffer();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    if (!capture.empty() && !captured) {
      std::vector<unsigned char> data(pixel_w() * pixel_h() * 3);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, pixel_w(), pixel_h(), GL_RGB, GL_UNSIGNED_BYTE,
                   data.data());
      auto [low, high] = std::minmax_element(data.begin(), data.end());
      if (*high - *low < 100)
        throw std::runtime_error("VTK output has no rendered geometry");
      std::ofstream out(capture, std::ios::binary);
      out << "P6\n" << pixel_w() << " " << pixel_h() << "\n255\n";
      for (int y = pixel_h() - 1; y >= 0; --y)
        out.write(reinterpret_cast<char *>(data.data() + y * pixel_w() * 3),
                  pixel_w() * 3);
      captured = true;
      std::cout << "PASS VTK rendered geometry into the FLTK OpenGL context"
                << std::endl;
    }
  }
  int handle(int event) override {
    if (event == FL_PUSH || event == FL_RELEASE || event == FL_DRAG ||
        event == FL_MOVE || event == FL_MOUSEWHEEL) {
      interactor->SetEventInformationFlipY(Fl::event_x(), Fl::event_y(),
                                           Fl::event_state(FL_CTRL),
                                           Fl::event_state(FL_SHIFT));
      if (event == FL_PUSH)
        interactor->InvokeEvent(
            Fl::event_button() == 1   ? vtkCommand::LeftButtonPressEvent
            : Fl::event_button() == 2 ? vtkCommand::MiddleButtonPressEvent
                                      : vtkCommand::RightButtonPressEvent);
      else if (event == FL_RELEASE)
        interactor->InvokeEvent(
            Fl::event_button() == 1   ? vtkCommand::LeftButtonReleaseEvent
            : Fl::event_button() == 2 ? vtkCommand::MiddleButtonReleaseEvent
                                      : vtkCommand::RightButtonReleaseEvent);
      else if (event == FL_MOUSEWHEEL)
        interactor->InvokeEvent(Fl::event_dy() < 0
                                    ? vtkCommand::MouseWheelForwardEvent
                                    : vtkCommand::MouseWheelBackwardEvent);
      else
        interactor->InvokeEvent(vtkCommand::MouseMoveEvent);
      redraw();
      return 1;
    }
    return Fl_Gl_Window::handle(event);
  }
};
int main(int argc, char **argv) {
  Window window;
  if (argc > 1)
    window.capture = argv[1];
  window.show();
  if (argc > 1)
    Fl::add_timeout(
        2, [](void *p) { static_cast<Window *>(p)->hide(); }, &window);
  return Fl::run();
}
