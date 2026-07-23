// SPDX-FileCopyrightText: 2026 Rao Jing
// SPDX-License-Identifier: GPL-3.0-only

#include "MainWindow.h"

#include <QApplication>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("DriveGuard-AI");
    QApplication::setApplicationVersion(DRIVEGUARD_VERSION);
    QApplication::setOrganizationName("HEU-Neusoft");

    QFont font("Microsoft YaHei UI");
    font.setPointSize(10);
    app.setFont(font);

    MainWindow window;
    window.resize(1380, 860);
    window.show();

    return app.exec();
}
