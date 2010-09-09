#include <QtGui/QApplication>
#include "spimview.h"

SpimView* Window;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    SpimView win;
    Window = &win;

    // Initialize Spim
    //
    message_out.i = 1;
    console_out.i = 2;

    win.sim_ReinitializeSimulator();

    win.show();

    return a.exec();
}