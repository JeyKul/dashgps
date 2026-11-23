#include "mainwindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("DashGPS");
    QApplication::setOrganizationName("dashgps");
    QApplication::setApplicationVersion(DASHGPS_VERSION);
    QApplication::setWindowIcon(QIcon(":/icon.svg"));

    MainWindow w;
    w.resize(1500, 800);
    w.show();
    QStringList args = app.arguments().mid(1);
    if (!args.isEmpty()) w.openPaths(args);
    // Developer hook for headless screenshots: DASHGPS_SCREENSHOT=out.png
    QString shot = qEnvironmentVariable("DASHGPS_SCREENSHOT");
    if (qEnvironmentVariableIsSet("DASHGPS_AUTOPLAY")) QTimer::singleShot(2000, &w, [&] { w.play(); });
    if (!shot.isEmpty())
        QTimer::singleShot(qEnvironmentVariableIntValue("DASHGPS_SHOT_MS") ? qEnvironmentVariableIntValue("DASHGPS_SHOT_MS") : 8000, &w, [&] { w.grab().save(shot); app.quit(); });
    return app.exec();
}
