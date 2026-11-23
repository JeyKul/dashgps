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
    if (!shot.isEmpty())
        QTimer::singleShot(8000, &w, [&] { w.grab().save(shot); app.quit(); });
    return app.exec();
}
