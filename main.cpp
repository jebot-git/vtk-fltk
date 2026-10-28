// SPDX-License-Identifier: MIT
#include "suite/desktop-menu.h"
#include "suite/gpu-window.h"
#include <FL/Fl_Button.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_ask.H>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vtk-fltk/RenderWidget.h>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkConeSource.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkGenericDataObjectReader.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkOBJReader.h>
#include <vtkPLYReader.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSTLReader.h>
#include <vtkSmartPointer.h>
#include <vtkXMLGenericDataObjectReader.h>
namespace {
struct ViewerWindow : dx::GpuWindow {
  using dx::GpuWindow::GpuWindow;
  std::function<bool()> key;
  int handle(int event) override {
    if ((event == FL_KEYDOWN || event == FL_SHORTCUT) && key && key())
      return 1;
    return dx::GpuWindow::handle(event);
  }
};
struct Viewer {
  ViewerWindow window{900, 660, "VTK FLTK"};
  vtk_fltk::RenderWidget view{0, 52, 900, 608};
  Fl_Menu_Bar menu{0, 0, 0, 0};
  vtkNew<vtkRenderer> renderer;
  vtkNew<vtkActor> actor;
  vtkNew<vtkDataSetMapper> mapper;
  std::unique_ptr<DesktopMenu> desktop;
  std::string report_path;
  enum Command { Open, Reset, Wireframe, Quit };
  Viewer() {
    window.callback(
        [](Fl_Widget *, void *data) { static_cast<Viewer *>(data)->close(); },
        this);
    window.key = [this] {
      int key = Fl::event_key(), state = Fl::event_state();
      if (state & (FL_ALT | FL_META))
        return false;
      if ((state & FL_CTRL) && key == 'o')
        command(Open);
      else if ((state & FL_CTRL) && key == 'w')
        command(Quit);
      else if (!(state & FL_CTRL) && key == 'w')
        command(Wireframe);
      else if (!(state & FL_CTRL) && key == 'r')
        command(Reset);
      else
        return false;
      return true;
    };
    menu.hide();
    menu.add("&File/&Open", FL_CTRL + 'o', invoke,
             reinterpret_cast<void *>(Open));
    menu.add("&File/&Close", FL_CTRL + 'w', invoke,
             reinterpret_cast<void *>(Quit));
    menu.add("&View/&Reset camera", 'r', invoke,
             reinterpret_cast<void *>(Reset));
    menu.add("&View/&Wireframe", 'w', invoke,
             reinterpret_cast<void *>(Wireframe));
    menu.user_data(this);
    auto *popup = new Fl_Menu_Button(12, 10, 90, 32, "Menu");
    popup->copy(menu.menu());
    popup->user_data(this);
    if (dx::dome())
      popup->hide();
    for (auto [label, command, x] :
         {std::tuple{"Open", Open, 112}, std::tuple{"Reset view", Reset, 212},
          std::tuple{"Wireframe", Wireframe, 322}}) {
      auto *button = new Fl_Button(x, 10, 100, 32, label);
      button->callback(
          [](Fl_Widget *w, void *p) {
            static_cast<Viewer *>(w->window()->user_data())
                ->command(Command(reinterpret_cast<intptr_t>(p)));
          },
          reinterpret_cast<void *>(command));
    }
    window.user_data(this);
    window.end();
    window.resizable(view);
    unsigned char r, g, b;
    Fl::get_color(FL_BACKGROUND2_COLOR, r, g, b);
    renderer->SetBackground(r / 255., g / 255., b / 255.);
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(.95, .65, .15);
    renderer->AddActor(actor);
    view.render_window()->AddRenderer(renderer);
    vtkNew<vtkConeSource> cone;
    cone->SetResolution(48);
    cone->Update();
    mapper->SetInputData(cone->GetOutput());
    renderer->ResetCamera();
  }
  ~Viewer() { close(); }
  void close() {
    view.hide();
    window.hide();
  }
  static void invoke(Fl_Widget *w, void *p) {
    auto *source = dynamic_cast<Fl_Menu_ *>(w);
    auto *self = static_cast<Viewer *>(source->user_data());
    self->command(Command(reinterpret_cast<intptr_t>(p)));
  }
  void command(Command cmd) {
    try {
      if (cmd == Open) {
        const char *file =
            fl_file_chooser("Open VTK geometry",
                            "*.{vtk,vtp,vtu,vti,vtr,vts,stl,obj,ply}", nullptr);
        if (file)
          load(file);
      } else if (cmd == Reset)
        renderer->ResetCamera();
      else if (cmd == Wireframe) {
        auto *p = actor->GetProperty();
        p->SetRepresentation(p->GetRepresentation() == VTK_WIREFRAME
                                 ? VTK_SURFACE
                                 : VTK_WIREFRAME);
      } else if (cmd == Quit)
        close();
      view.redraw();
    } catch (const std::exception &e) {
      fl_alert("%s", e.what());
    }
  }
  template <class Reader>
  vtkSmartPointer<vtkDataSet> read(const std::string &path) {
    vtkNew<Reader> reader;
    reader->SetFileName(path.c_str());
    reader->Update();
    auto *data = vtkDataSet::SafeDownCast(reader->GetOutputDataObject(0));
    if (reader->GetErrorCode() != 0 || !data || data->GetNumberOfPoints() == 0)
      throw std::runtime_error("No readable dataset geometry in " + path);
    return data;
  }
  void load(const std::string &path) {
    auto extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    vtkSmartPointer<vtkDataSet> data;
    if (extension == ".vtk")
      data = read<vtkGenericDataObjectReader>(path);
    else if (extension == ".stl")
      data = read<vtkSTLReader>(path);
    else if (extension == ".ply")
      data = read<vtkPLYReader>(path);
    else if (extension == ".obj")
      data = read<vtkOBJReader>(path);
    else if (extension == ".vtp" || extension == ".vtu" ||
             extension == ".vti" || extension == ".vtr" || extension == ".vts")
      data = read<vtkXMLGenericDataObjectReader>(path);
    else
      throw std::runtime_error(
          "Supported files: VTK, VTP/VTU/VTI/VTR/VTS, STL, OBJ and PLY");
    mapper->SetInputData(data);
    renderer->ResetCamera();
    view.redraw();
    window.copy_label(
        (std::filesystem::path(path).filename().string() + " — VTK FLTK")
            .c_str());
    std::cout << "Loaded " << data->GetNumberOfPoints() << " points, "
              << data->GetNumberOfCells() << " cells\n";
  }
  void show() {
    window.xclass("vtk-fltk");
    window.show();
    desktop = std::make_unique<DesktopMenu>(window, menu);
    if (!report_path.empty())
      Fl::add_timeout(.1, tick, this);
  }
  static void tick(void *data) {
    auto *self = static_cast<Viewer *>(data);
    auto *input = self->mapper->GetInput();
    auto *camera = self->renderer->GetActiveCamera()->GetPosition();
    Json state = {
        {"points", input ? input->GetNumberOfPoints() : 0},
        {"cells", input ? input->GetNumberOfCells() : 0},
        {"camera", {camera[0], camera[1], camera[2]}},
        {"wireframe",
         self->actor->GetProperty()->GetRepresentation() == VTK_WIREFRAME},
        {"capture_complete", self->view.capture_complete()},
        {"desktop_menu",
         {{"service", self->desktop->serviceName()},
          {"path", self->desktop->objectPath()}}}};
    auto temporary = self->report_path + ".tmp";
    {
      std::ofstream out(temporary);
      out << state.dump(2) << '\n';
    }
    std::filesystem::rename(temporary, self->report_path);
    if (self->window.shown())
      Fl::repeat_timeout(.1, tick, self);
  }
};
} // namespace
int main(int argc, char **argv) {
  try {
    std::string file, capture, report;
    int duration = 0;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--help") {
        std::cout << "vtk-fltk [GEOMETRY] [--capture PPM] [--report JSON] "
                     "[--quit-after-ms N]\n";
        return 0;
      }
      if (arg == "--capture" && i + 1 < argc)
        capture = argv[++i];
      else if (arg == "--report" && i + 1 < argc)
        report = argv[++i];
      else if (arg == "--quit-after-ms" && i + 1 < argc)
        duration = std::stoi(argv[++i]);
      else if (arg.starts_with('-'))
        throw std::runtime_error("Unknown or incomplete option: " + arg);
      else if (file.empty())
        file = arg;
      else
        throw std::runtime_error("Open one geometry file at a time");
    }
    if (!getenv("FLTK_BACKEND"))
      setenv("FLTK_BACKEND", "wayland", 0);
    dx::init();
    Fl::lock();
    Viewer viewer;
    viewer.report_path = report;
    if (!file.empty())
      viewer.load(file);
    if (!capture.empty())
      viewer.view.capture(capture);
    viewer.show();
    if (duration > 0)
      Fl::add_timeout(
          duration / 1000., [](void *p) { static_cast<Viewer *>(p)->close(); },
          &viewer);
    int result = Fl::run();
    if (!capture.empty() && !viewer.view.capture_complete())
      throw std::runtime_error("No VTK frame captured");
    return result;
  } catch (const std::exception &e) {
    std::cerr << "VTK FLTK: " << e.what() << '\n';
    return 1;
  }
}
