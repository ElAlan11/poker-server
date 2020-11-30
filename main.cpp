#include <QCoreApplication>
#include "pokergame.h"
#include "player.h"
#include <iostream>
#include <algorithm>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkInterface>


int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    PokerGame pg;

    return a.exec();
}
