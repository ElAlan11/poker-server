#include "pokergame.h"

PokerGame::PokerGame(QObject *parent) : QObject(parent)
{
    contPlayers = 0;
    sndRound = false;
    gameStarted = false;
    tcpServer = new QTcpServer(this);

    if(!tcpServer->listen(QHostAddress::Any, 49300)){
        cout << "Unable to start the server";
        return;
    }
    else{
        cout << "Server started" << endl;
    }

    QString ipAddress;
    QList<QHostAddress> ipAddressesList = QNetworkInterface::allAddresses();

    // To use the first non-localhost IPv4 address
    for (int i = 0; i < ipAddressesList.size(); ++i) {
        if (ipAddressesList.at(i) != QHostAddress::LocalHost &&
                ipAddressesList.at(i).toIPv4Address()) {
            ipAddress = ipAddressesList.at(i).toString();
            break;
        }
    }
    if (ipAddress.isEmpty()) // If we did not find one, use IPv4 localhost
        ipAddress = QHostAddress(QHostAddress::LocalHost).toString();

    cout << "Server IP: " << ipAddress.toStdString() << " - Server port: " << tcpServer->serverPort() << endl;
    QObject::connect(tcpServer, &QTcpServer::newConnection, this, &PokerGame::playerConnected);


    QEventLoop loop;
    loop.connect(this, SIGNAL(fullRoom()), SLOT(quit()));
    loop.exec();

    gameStarted = true;

    for(unsigned int i=0; i<players.size(); i++){
        players[i].playerNum = i+1;

        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);
        out << (qint8)2;
        out << (qint8)(i+1);
        out << (qint8)players.size();
        for(unsigned int j=0; j<players.size(); j++)
            out << QString::fromStdString(players[j].nickname);
        players[i].socket->write(block);
        players[i].socket->flush();
    }

//    //Successives matches ends when there's only one player remaining
    while(contPlayers > 1 && newGame());

    return;
}


PokerGame::valToNumMap PokerGame::valToNum = {
    {'2',2},{'3',3},{'4',4},{'5',5},{'6',6},{'7',7},{'8',8},{'9',9},{'0',10},{'J',11},{'Q',12},{'K',13},{'A',14}
};

bool PokerGame::compareCards(const char* c1, const char* c2){
    return valToNum[c1[1]] < valToNum[c2[1]]; // Desc
}

bool PokerGame::compareBets(const Player p1, const Player p2){
    return p1.bet < p2.bet; // Desc
}

bool PokerGame::newGame(){
    if(sndRound){
        QEventLoop loop;
        loop.connect(this, SIGNAL(allPlayersRes()), SLOT(quit()));
        loop.exec();
    }

    playAgainResCount = 0;
    suits.clear();
    values.clear();
    deck.clear();

    turn = 0;
    pot = 0;
    subPots = false;

    suits = {'S', 'C', 'H', 'D'};
    values = {'2', '3', '4', '5', '6', '7', '8', '9', '0', 'J', 'Q', 'K', 'A'};

    //Fills the cards dictonary/map
    for(auto s:suits)
        deck[s] = values;

    //Set the community cards
    for(int i=0; i<5; i++){
        char* res = dealRandomCard(i);
        commCards[i][0] = res[0];
        commCards[i][1] = res[1];
    }

    //Deal players cards
    for(unsigned int i=0; i<players.size(); i++){
        char* res[2];
        res[0] = dealRandomCard(0+i*i);
        res[1] = dealRandomCard(1+i*i);
        players[i].setCards(res);
    }

    //Initial bet
    for(int i=0; i<contPlayers; i++){
        if(players[i].stack < INITIAL_BET){

            // -Jugador dejó la partida(10)”- PACKAGE to ALL PLAYERS
            pkgPlayerEliminated(i);
//            players.erase(players.begin()+i);
//            i--;
//            contPlayers--;

//            if(contPlayers < 2){
//                // -Partida terminada- PACKAGE to ALL PLAYERS Code 2
//                pkgGameOver(2);
//                return false;
//            }
        }
        else{
            players[i].isAllin = false;
            players[i].isOut = false;
            players[i].secPot = 0;

            players[i].bet = INITIAL_BET;
            players[i].stack -= INITIAL_BET;

            // - New match - PACKAGE
            pkgNewMatch(i);
        }
    }

    // Update status
    pkgGlobalState();

    // PRE FLOP
    if(!betsRound())
        return false;

    // THE FLOP
    // -Cartas comunitarias- PACKAGE to ALL PLAYERS
    pkgCommCards();
    if(!betsRound())
        return false;

    // THE TURN
    // -Abrir carta- PACKAGE to ALL PLAYERS
    pkgOpenCard(3);
    if(!betsRound())
        return false;

    // THE RIVER
    // -Abrir carta- PACKAGE to ALL PLAYERS
    pkgOpenCard(4);
    if(!betsRound())
        return false;

    int winner = findWinner();
    if(subPots)
        players[winner].stack += players[winner].secPot;
    else
        players[winner].stack += pot;

    turn = 0;
    sndRound = true;

    // -Showdown- PACKAGE to ALL
    pkgShowdown(players[winner].playerNum);

    return true;
}

bool PokerGame::betsRound(){
    int lastBet = 0;

    if(players[0].bet == INITIAL_BET)
        lastBet = INITIAL_BET;
    else{
//        for(auto it = players.begin(); it!=players.end(); it++)
//            it->bet = 0;
    }

    int noRaisedCount = 0;

    auto it = players.begin()+turn;
    bool firstCycle = true;

    while(noRaisedCount != contPlayers){
        if(!firstCycle)
            it = players.begin();
        for(; it!=players.end(); it++){
            firstCycle = false;

            int contPlayersOut=0;
            for(unsigned int i=0; i<players.size(); i++)
                if(players[i].isOut)
                    contPlayersOut++;
            if(contPlayersOut == contPlayers-1){
                noRaisedCount = contPlayers;
                break;
            }

            if(noRaisedCount == contPlayers)
                break;

            if(it->isAllin || it->isOut){
                noRaisedCount++;
                continue;
            }
            //  -Turno de- PACKAGE
            pkgTurnOf(it->playerNum);

            // 0=Out, 1=Check/Call, 2=Raise, 3=All-in

            // Wait for respective player response and verify the origin
            QEventLoop loop;
            loop.connect(this, SIGNAL(turnPlayed()), SLOT(quit()));
            loop.exec();

            if(ammount > it->stack){
                // -Game over- PACKAGE to ALL PLAYERS Code 1
                pkgGameOver(1);
                return false;
            }

            if(senderPl != it->playerNum){
                pkgGameOver(1);
                return false;
            }

            switch(action){
            case 0:
                it->isOut = true;
                noRaisedCount++;
                break;
            case 1:
                it->bet += ammount;
                it->stack -= ammount;
                noRaisedCount++;
                break;
            case 2:
                lastBet = it->bet + ammount;
                it->bet += ammount;
                it->stack -= ammount;
                noRaisedCount = 0;
                break;
            case 3:
                it->isAllin = true;
                it->bet += ammount;
                it->stack -= ammount;

                if(it->bet < lastBet){
                    subPots = true;
                    noRaisedCount++;
                }
                else if (it->bet == lastBet){
                    noRaisedCount++;
                }
                else{
                    lastBet = it->bet;
                    noRaisedCount = 0;
                }
                break;
            }

            // -Turno jugado- PACKAGE to ALL
            pkgTurnPlayed(it->playerNum, lastBet);

            //  -Estatus global- PACKAGE to ALL
            pkgGlobalState();
        }
    }

    if(subPots == true){
        vector<Player> sortedPlayers = players;
        std::sort(sortedPlayers.begin(), sortedPlayers.end(), compareBets);
        int prevPot = 0;
        int prevBet = 0;

        for(int i=0; i<contPlayers; i++){
            unsigned int j=0;
            for(j=0; j<players.size(); j++) // Finds the original vector index
                if(players[j].playerNum == sortedPlayers[i].playerNum)
                    break;

//            int increment = (sortedPlayers[i].bet - prevBet) * (contPlayers-i);
//            players[j].secPot += pot + prevPot + increment;
//            prevPot = increment;

            players[j].secPot = prevPot + (sortedPlayers[i].bet - prevBet) * (contPlayers-i);
            prevPot = players[j].secPot; // OJO AQUI para cuando es
            prevBet = sortedPlayers[i].bet;
        }
        pot = 0; // Subsequent bet rounds will not sum again the general pot
    }
    else{
        pot = 0;
        for(auto p: players)
            pot += p.bet;
    }
    pkgGlobalState();
    turn = (it - players.begin()); // Set the next player
    if (turn == contPlayers)
        turn = 0;
    return true;
}

char* PokerGame::dealRandomCard(int s){
    char* randomCard;
    randomCard = new char[2];

    for(int i=0; i<1; i++){ // .. To give the 2 holecards to the player
        bool emptySuit = false;
        char card[2] = {'X', 'X'};

        while(card[1] == 'X'){
            srand((unsigned) time(0) + s*3); // Random seed

            int suitIndex = rand() % suits.size(); // Selects a random non empty suit
            card[0] = suits[suitIndex];

            size_t cardsRemaining = deck[card[0]].size(); // ... Cards remaining for a given suit

            if(!cardsRemaining) // If there're no more cards for that suit
            {
                suits.erase(suits.begin() + suitIndex); // Deletes the empty suit
                emptySuit = true;
                //emptySuits++; 4 TEST PURPOSES
                break;
            }

            int cardIndex = rand() % cardsRemaining;
            card[1] = deck[card[0]][cardIndex]; // Selects a random card available in the picked random suit
            deck[card[0]].erase(deck[card[0]].begin() + cardIndex); // Delete the just taken card
        }

        if(!emptySuit){
            randomCard[0] = card[0];
            randomCard[1] = card[1];
        }
    }

    return randomCard;
}

void PokerGame::whichHand(Player& p){
    map<char,int> suitsCount; // Counts the number of cards of each suit
    map<char,int> valuesCount; // Counts the number of cards of each value

    vector<char*> gameCards;

    for(int i=0;i<5; i++){
        char* temp = new char[2];
        temp[0] = commCards[i][0];
        temp[1] = commCards[i][1];
        gameCards.push_back(temp);
    }
    for(int i=0;i<2; i++){
        char* temp = new char[2];
        temp[0] = p.holeCards[i][0];
        temp[1] = p.holeCards[i][1];
        gameCards.push_back(temp);
    }


    //Sort the cards by their values
    std::sort(gameCards.begin(), gameCards.end(), compareCards);

    cout << endl;

    // Fills suitsCount and valuesCont
    for(auto c : gameCards){
        suitsCount[c[0]]+=1;
        valuesCount[c[1]]+=1;
        cout << c[0] << c[1] << endl;
    }

    size_t i = gameCards.size()-1;
    int seqCount = 1;
    char maxInSeq = gameCards[gameCards.size()-1][1]; // Higher card in the sequence

    while(i > 0 && seqCount != 5) { // Find the greatest sequence in the cards
        if(valToNum[gameCards[i][1]] == valToNum[gameCards[i-1][1]]){ // If a number is repeated it is ignored
            i--;
            continue;
        }
        else if(valToNum[gameCards[i][1]] == valToNum[gameCards[i-1][1]]+1){ // Check if the card values are in a sequence
            seqCount++;
        }
        else{
            maxInSeq = gameCards[i-1][1];
            seqCount = 1;
        }
        i--;
    }

    /* HANDS RANK
     *
     * hand[0] = Hand
     * hand[1] = MaxValue
     *
     * 10 = Royal flush
     * 9 = Straight Flush
     * 8 = Four of a kind
     * 7 = Full house
     * 6 = Flush
     * 5 = Straight
     * 4 = Three of a kind
     * 3 = Two pair
     * 2 = Pair
     * 1 = High card
     */

    if(seqCount==5){
        p.hand[0] = 5; // ----- STRAIGHT -----
        p.hand[1] = valToNum[maxInSeq];
    }
    else if(seqCount==4 && valuesCount['A']>0 && maxInSeq == '5'){ // If the sequence is an A-5 straight
        p.hand[0] = 5; // ----- STRAIGHT -----
        p.hand[1] = valToNum[maxInSeq];
    }
    else{
        for(auto e : valuesCount){
            if(e.second >= 4){
                p.hand[0] = 8;
                p.hand[1] = valToNum[e.first]; // ----- FOUR OF A KIND -----
                break;
            }
            else if(e.second == 3){
                if(p.hand[0] == 2){ // If we alredy have a pair
                    p.hand[1] = valToNum[e.first];
                    p.hand[0] = 7; // ----- FULL HOUSE -----
                    break;
                }
                if(p.hand[0]==4){ // If we have another three of a kind
                    p.hand[0] = 7;
                    if(valToNum[e.first] > p.hand[1]) //If prev. TOK is higher...
                        p.hand[1] = valToNum[e.first];
                    break;
                }
                p.hand[1] = valToNum[e.first];
                p.hand[0] = 4; // ----- THREE OF A KIND -----
            }
            else if(e.second==2){
                if(p.hand[0]==4){ // If we alredy have three of a kind
                    p.hand[0] = 7; // ----- FULL HOUSE -----
                    break;
                }
                if(p.hand[0]==3){ // If we alredy have two pairs
                    int hPair = p.hand[1]/100;
                    int lPair = p.hand[1]%100;

                    if(valToNum[e.first] > hPair-20){
                        p.hand[1] = (valToNum[e.first]+20)*100 + hPair;
                    }
                    else if(valToNum[e.first] > lPair-20){
                        p.hand[1] = p.hand[1]-lPair+valToNum[e.first];
                    }
                    break;
                }
                if(p.hand[0]==2){ // If we have another pair
                    p.hand[0] = 3; // ----- TWO PAIRS -----

                    /* 2-PAIRS/2-TOAK  - HAND VALUE FORMAT:
                     * Stored value = Real value + 20 -> To for a 2-digit format
                     * Real value = Stored value - 20
                     * (2) Most significant digits = Higher pair value
                     * (2) Less significant digits = Lower pair value
                     * I.e. 3322 -> 33 - 20 = 13 (Higher pair value is K) and 22 - 20 = 2 (Lower pair value is 2)
                     */

                    if(p.hand[1] > valToNum[e.first]){ //If prev. pair is higher...
                        p.hand[1] = (p.hand[1]+20)*100; //Rotate two digits to the left
                        p.hand[1] += valToNum[e.first]+20;
                    }
                    else{
                        p.hand[1] += 20;
                        p.hand[1] += (valToNum[e.first]+20)*100;
                    }
                    continue;
                }
                p.hand[0] = 2; // ----- PAIR -----
                p.hand[1] = valToNum[e.first];
            }
        }
    }

    for(auto e : suitsCount){
        if(e.second >= 5){ // If there are more than 5 cards of a suit
            if(p.hand[0] == 5){
                size_t maxIndex = gameCards.size()-1;
                int contColor = 0;
                while(gameCards[maxIndex][1] != maxInSeq && maxIndex>0) maxIndex--;

                while(valToNum[gameCards[maxIndex][1]] > valToNum[maxInSeq]-5){
                    if(gameCards[maxIndex][0] == e.first)
                        contColor++;
                    maxIndex--;
                    if(maxIndex<0)
                        break;
                }
                if(maxInSeq == '5'){ // A-5 Straight CASE
                    maxIndex = gameCards.size()-1;
                    while(gameCards[maxIndex][1] == 'A'){
                        if(gameCards[maxIndex][0] == e.first)
                            contColor++;
                        maxIndex--;
                    }
                }
                if(contColor>=5){
                    if(maxInSeq == 'A')
                        p.hand[0] = 10; // ----- ROYAL FLUSH -----
                    else
                        p.hand[0] = 9; // ----- STRAIGHT FLUSH -----
                    break;
                }
            }
            if(p.hand[0] < 6){ // If the current hand is worse than FLUSH set FLUSH as the current hand
                for(size_t it = gameCards.size()-1; it>=0; it--){ // Look for the higher card in the FLUSH
                    if(gameCards[it][0] == e.first){
                        p.hand[0] = 6; // ----- FLUSH -----
                        p.hand[1] = valToNum[gameCards[it][1]];
                        break;
                    }
                }
            }
            break;
        }
    }

    if(p.hand[0] < 1){
        p.hand[0] = 1; // ----- HIGHER CARD -----
        if(valToNum[p.holeCards[0][1]] > valToNum[p.holeCards[1][1]])
            p.hand[1] = valToNum[p.holeCards[0][1]];
        else
            p.hand[1] = valToNum[p.holeCards[1][1]];
    }

    cout << "Num: " << p.playerNum << "Hand: " << p.hand[0] << " Max: " << p.hand[1] << endl;
    return;
}

int PokerGame::findWinner(){
    int greatest=0, indGreatest=0;

    for(auto it = players.begin(); it != players.end(); it++)
        whichHand(*it);

    for(unsigned int i=0; i<players.size(); i++){
        if(players[i].isOut)
            continue;

        if(players[i].hand[0] > greatest){
            greatest = players[i].hand[0];
            indGreatest = i;
        }
        else if(players[i].hand[0] == greatest){ // Same hand
            if(players[i].hand[1] > players[indGreatest].hand[1]){
                greatest = players[i].hand[0];
                indGreatest = i;
            }
            else if(players[i].hand[1] == players[indGreatest].hand[1]){ // Same hand value (i.e. 2 pairs of 2)
                if(players[i].higherCardVal() > players[indGreatest].higherCardVal()){ // Higher card wins
                    greatest = players[i].hand[0];
                    indGreatest = i;
                }
                else if(players[i].higherCardVal() == players[indGreatest].higherCardVal()){
                    if(players[i].lowerCardVal() > players[indGreatest].lowerCardVal()){ // Higher card wins
                        greatest = players[i].hand[0];
                        indGreatest = i;
                    }
                }
                else{
                    //Empate
                }
            }
        }
    }
    return indGreatest;
}

void PokerGame::playerConnected(){
    QTcpSocket *socket = tcpServer->nextPendingConnection();

    if(gameStarted || contPlayers==4){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)12;
        socket->write(block);
        socket->flush();
        socket->close();
        return;
    }

    Player p(socket);
    players.push_back(p);

    connect(socket, &QTcpSocket::readyRead, this, &PokerGame::packageReceived);
    connect(socket, &QTcpSocket::disconnected, this, &PokerGame::playerDisconnected);

    cout << "Player connected" << endl;
}

void PokerGame::playerDisconnected(){
    QTcpSocket* readSocket = qobject_cast<QTcpSocket*>(sender());

    size_t i=0;
    for(;i<players.size(); i++)
        if(players[i].socket == readSocket)
            break;

    int playerNum = players[i].playerNum;
    players.erase(players.begin()+i);
    contPlayers--;
    readSocket->close();

    if(!gameStarted){
        cout << "Player disconnected" << endl;
    }
    else{
        cout << "Player disconnected" << endl;

        if(contPlayers < 2){
            pkgGameOver(2);
            QCoreApplication::quit();
        }

        pkgPlayerDisconnected(playerNum);
        if(turn == contPlayers)
            turn = 0;
    }
}

void PokerGame::packageReceived(){
    QTcpSocket* readSocket = qobject_cast<QTcpSocket*>(sender());
    size_t i=0;
    for(;i<players.size(); i++)
        if(players[i].socket == readSocket)
            break;
    Player* player = &players[i];

    cout << "Bytes recibidos: "<< readSocket->bytesAvailable() << endl;

    QByteArray bytearray = readSocket->readAll();
    QDataStream in(bytearray);
    in.setVersion(QDataStream::Qt_4_0);

    bool endReached = false;

    while(!endReached){
        qint8 pkgCode;
        in >> pkgCode;

        cout << "Package code: " << (int)pkgCode << endl;

        switch(pkgCode){

        case 1: // Unirse a sala de espera
        {
            QString nickname;
            in >> nickname;

            player->nickname = nickname.toStdString();
            contPlayers++;

            if(contPlayers >= MIN_PLAYERS)
                emit fullRoom();

            cout << nickname.toStdString() << contPlayers <<endl;
            break;
        }

        case 6: // Turno jugado
        {
            qint8 nPlayer;
            in >> nPlayer;
            qint8 move;
            in >> move;
            qint16 money;
            in >> money;

            action = move;
            ammount = money;
            senderPl = nPlayer;

            emit turnPlayed();
            break;
        }

        case 14: // Mensaje del chat
        {
            QString str2;
            QString str = QString::fromStdString(player->nickname) + ": ";
            in >> str2;
            str += str2;
            pkgMessage(str);
            break;
        }

        case 15: // Jugar de nuevo
        {
            qint8 res;
            in >> res;

            if((contPlayers == 3 && playAgainResCount == 2) || (contPlayers == 4 && playAgainResCount == 3)){
                readSocket->waitForDisconnected(1500);
                emit allPlayersRes();
            }


            if(res == 1)
                playAgainResCount++;

            if(playAgainResCount == contPlayers)
                emit allPlayersRes();

            break;
        }

        default:
            break;
        }

        if(in.atEnd())
            endReached = true;
    }
}

void PokerGame::pkgPlayerEliminated(int nPlayer){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)10;
        out << (qint8)players[nPlayer].playerNum;
        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgPlayerDisconnected(int nPlayer){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)13;
        out << (qint8)nPlayer;
        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgGameOver(int reason){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)11;
        out << (qint8)reason;
        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgNewMatch(int nPlayer){
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_4_0);

    out << (qint8)3;
    out << (qint8)players[nPlayer].holeCards[0][0];
    out << (qint8)players[nPlayer].holeCards[0][1];
    out << (qint8)players[nPlayer].holeCards[1][0];
    out << (qint8)players[nPlayer].holeCards[1][1];

    players[nPlayer].socket->write(block);
    players[nPlayer].socket->flush();
}

void PokerGame::pkgGlobalState(){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)4;
        out << (qint8)contPlayers;

        if(!subPots)
            out << (qint16)pot;
        else
            out << (qint16)players[i].secPot;

        for(unsigned int j=0; j<players.size(); j++){
            out << (qint8)players[j].playerNum;
            out << (qint16)players[j].bet;
            out << (qint16)players[j].stack;
        }

        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgTurnOf(int nPlayer){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)5;
        out << (qint8)nPlayer;

        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgTurnPlayed(int nPlayer, int bet){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)6;
        out << (qint8)nPlayer;
        out << (qint8)action;
        out << (qint16)bet;

        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgCommCards(){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)7;
        out << (qint8)commCards[0][0];
        out << (qint8)commCards[0][1];
        out << (qint8)commCards[1][0];
        out << (qint8)commCards[1][1];
        out << (qint8)commCards[2][0];
        out << (qint8)commCards[2][1];
        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgOpenCard(int indexCard){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)8;
        out << (qint8)commCards[indexCard][0];
        out << (qint8)commCards[indexCard][1];
        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgShowdown(int numWinner){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)9;
        out << (qint8)contPlayers;
        out << (qint8)numWinner;

        if(!subPots)
            out << (qint16)pot;
        else{
            unsigned int j=0;
            for(; j<players.size(); j++)
                if(players[j].playerNum == numWinner)
                    break;
            out << (qint16)players[j].secPot;
        }

        for(unsigned int j=0; j<players.size(); j++){
            out << (qint8)players[j].playerNum;
            out << (qint8)players[j].holeCards[0][0];
            out << (qint8)players[j].holeCards[0][1];
            out << (qint8)players[j].holeCards[1][0];
            out << (qint8)players[j].holeCards[1][1];
            out << (qint16)players[j].hand[0];
            out << (qint16)players[j].hand[1];
        }

        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgMessage(QString str){
    for(unsigned int i=0; i<players.size(); i++){
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);

        out << (qint8)14;
        out << (QString)str;

        players[i].socket->write(block);
        players[i].socket->flush();
    }
}

void PokerGame::pkgPlayAgain(int index, int res){
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_4_0);

    out << (qint8)15;
    out << (qint8)res;
    players[index].socket->write(block);
    players[index].socket->flush();
}
