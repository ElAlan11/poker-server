#ifndef POKERGAME_H
#define POKERGAME_H

#include <QObject>
#include "player.h"
#include <iostream>
#include <algorithm>
#include "qstring.h"
#include "qtextstream.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkInterface>
#include <QDataStream>

class PokerGame : public QObject
{
    Q_OBJECT
private:
    static const int INITIAL_BET = 5;

    int turn;
    int pot;
    bool subPots;

    int contPlayers;
    vector<Player> players;

    char commCards[5][2] = {{'X'}};
    vector<char> suits;
    vector<char> values;
    map<char, vector<char>> deck;

    QTcpServer *tcpServer = nullptr;

private slots:
    void playerConnected();
    void readyRead();

public:
    explicit PokerGame(QObject *parent = nullptr);

    typedef map<char,int> valToNumMap;
    static valToNumMap valToNum;
    bool static compareCards(const char* card1, const char* card2);
    bool static compareBets(const Player p1, const Player p2);

    bool newGame();
    bool betsRound();
    char* dealRandomCard(int s);
    void whichHand(Player& p);

signals:

};

#endif // POKERGAME_H
