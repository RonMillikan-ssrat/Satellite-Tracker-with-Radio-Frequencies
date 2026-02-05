#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // Set application metadata
    QApplication::setApplicationName("Satellite Tracker");
    QApplication::setApplicationVersion("1.0");
    QApplication::setOrganizationName("SatTrack");
    
    MainWindow window;
    window.show();
    
    return app.exec();
}
