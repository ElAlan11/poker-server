#include "player.h"

Player::Player()
{
    isAllin = false;
    isOut = false;
    playerNum = 0;
    nickname = "Default";
    stack = 9999;
    bet = 0;
    secPot = 0;
}

Player::Player(QTcpSocket *skt)
{
    socket = skt;
    isAllin = false;
    isOut = false;
    stack = 1000;
    secPot = 0;
}

Player::Player(int number, string name)
{
    playerNum = number;
    nickname = name;
    isAllin = false;
    isOut = false;
    stack = 1000;
    secPot = 0;
}


void Player::setCards(char** cards){
    holeCards[0][0] = cards[0][0];
    holeCards[0][1] = cards[0][1];
    holeCards[1][0] = cards[1][0];
    holeCards[1][1] = cards[1][1];

    hand[0] = 0;
    hand[1] = 0;

    bet = 0;
}

int Player::higherCardVal(){
    if(holeCards[0][1] > holeCards[1][1])
        return holeCards[0][1];
    else
        return holeCards[1][1];
}

int Player::lowerCardVal(){
    if(holeCards[0][1] < holeCards[1][1])
        return holeCards[0][1];
    else
        return holeCards[1][1];
}

ostream& operator<<(ostream& os, const Player& p)
{
    os << endl << "Player: " << p.playerNum << endl
         << "Nickname: " << p.nickname << endl
         << "Stack: " << p.stack << endl
         << "Sec. Pot: " << p.secPot << endl
         << "Hole Cards: " << p.holeCards[0][0] << p.holeCards[0][1] << " "
                           << p.holeCards[1][0] << p.holeCards[1][1] << endl
         << "Is all-in: " << p.isAllin << endl
         << "Is out: " << p.isOut << endl
         << "Bet: " << p.bet << endl
         << "Hand: " << p.hand[0] << " " << p.hand[1] << endl;
    return os;
}
