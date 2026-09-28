#include "MainWindow.h"
#include <QApplication>
#include <iostream>
#include <string>

#if defined(HRG_WITH_THERMALFIST)
#include "HRGBase/Utility.h"
#endif

namespace {

std::string formatBoxLine(const std::string &content = "") {
  std::string line = "#";
  if (content.empty()) {
    line += std::string(77, ' ');
  } else {
    line += " " + content;
    if (line.size() < 78) {
      line += std::string(78 - line.size(), ' ');
    }
  }
  line += "#";
  return line;
}

void printGuiBanner() {
  static bool printed = false;
  if (printed) return;
  printed = true;

  const std::string border(79, '#');
  std::cout << border << "\n"
            << formatBoxLine() << "\n"
            << formatBoxLine("Cosmic Trajectories GUI - Early Universe QCD Phase Transition") << "\n"
            << formatBoxLine("Version 1.0") << "\n"
            << formatBoxLine() << "\n"
            << formatBoxLine("Developer: Lorenzo Formaggio <formaggio.lorenzo@gmail.com>") << "\n"
            << formatBoxLine() << "\n"
            << formatBoxLine("Numerical solver for cosmic trajectories, conserved charges (B, Q, L),") << "\n"
            << formatBoxLine("lepton asymmetries, and QCD Equations of State.") << "\n"
            << formatBoxLine() << "\n"
            << formatBoxLine("Repository:") << "\n"
            << formatBoxLine("https://github.com/Lorenzo-Formaggio/GUI-Cosmic-Trajectories-cpp") << "\n"
            << formatBoxLine() << "\n"
            << border << "\n\n" << std::flush;

#if defined(HRG_WITH_THERMALFIST)
  // Ensure Thermal-FIST prints its disclaimer right after ours
  thermalfist::Disclaimer::PrintDisclaimer();
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor(101)))
static void earlyBannerInit() {
  printGuiBanner();
}
#endif

} // anonymous namespace

int main(int argc, char *argv[]) {
  // Ensure banner is printed even if constructor attributes are not invoked
  printGuiBanner();

  // Set application metadata
  QApplication::setApplicationName("Cosmic Trajectories GUI");
  QApplication::setApplicationVersion("1.0");

  QApplication app(argc, argv);

  MainWindow window;
  window.showMaximized();

  return app.exec();
}

