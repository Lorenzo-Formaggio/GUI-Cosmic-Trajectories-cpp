#include "MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
  // Set application metadata
  QApplication::setApplicationName("Cosmic Trajectories GUI");
  QApplication::setApplicationVersion("1.0");

  QApplication app(argc, argv);

  MainWindow window;
  window.resize(1000, 700);
  window.show();

  return app.exec();
}
