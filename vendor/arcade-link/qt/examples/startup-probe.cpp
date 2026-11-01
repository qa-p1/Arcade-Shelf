// Isolated critical-path probe; worker completion and icon paint are excluded.
#include "ArcadeInstall.h"
#include "ArcadeTrayHost.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <iostream>
using namespace ArcadeLink;

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (qEnvironmentVariable("ARCADE_E2E_INNER") != "1") {
    std::cerr << "run through tools/e2e.py run --\n";
    return 2;
  }
  ArcadeInstall::Environment environment;
  QString error;
  if (!ArcadeInstall::Environment::discover(&environment, &error)) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  ArcadeInstall installer(environment);
  const auto runtime = ArcadeInstall::Runtime::discover();
  QJsonArray samples;
  for (int i = 0; i < 20; ++i) {
    QElapsedTimer timer;
    timer.start();
    ArcadeInstall::Detection detection;
    if (!installer.detect(Ids::Shelf, runtime, &detection, &error)) {
      std::cerr << error.toStdString() << '\n';
      return 1;
    }
    const double detected = timer.nsecsElapsed() / 1000000.0;
    {
      ArcadeTrayHost watcher(Locations::discover(), Ids::Shelf);
      watcher.start();
      samples.append(QJsonObject{{"detectMs", detected},
                                 {"combinedMs", timer.nsecsElapsed() / 1000000.0}});
    }
    QEventLoop settle;
    QTimer::singleShot(20, &settle, &QEventLoop::quit);
    settle.exec();
  }
  const QJsonObject result{
      {"samples", samples}, {"budgetMs", 5},
      {"method", "Qt detect() + ArcadeTrayHost construction/start return; excludes worker completion and icon paint"}};
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData()
            << '\n';
  return 0;
}
