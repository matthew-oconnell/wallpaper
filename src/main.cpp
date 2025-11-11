#include <QApplication>
#include "appwindow.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    AppWindow w;
    w.show();
    return app.exec();
}
