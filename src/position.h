#pragma once

#include <QVector>
#include <QString>
#include <array>

namespace Piece {
constexpr char Empty = '.';
constexpr char King = 'K';
constexpr char Advisor = 'A';
constexpr char Elephant = 'B';
constexpr char Horse = 'N';
constexpr char Rook = 'R';
constexpr char Cannon = 'C';
constexpr char Pawn = 'P';

bool isRed(char piece);
bool isBlack(char piece);
bool sameColor(char a, char b);
char typeOf(char piece);
char make(bool red, char type);
QString displayName(char piece);
}

struct Move {
    int from = -1;
    int to = -1;

    bool isValid() const { return from >= 0 && from < 90 && to >= 0 && to < 90; }
    bool operator==(const Move &other) const { return from == other.from && to == other.to; }
};

class Position {
public:
    Position();

    static Position initial();
    static Position empty();
    static bool fromFen(const QString &fen, Position &position, QString *error = nullptr);

    QString toFen() const;
    void clear();
    void setPiece(int square, char piece);
    char piece(int square) const;
    char piece(int row, int col) const;

    QVector<Move> legalMoves() const;
    QVector<Move> legalMovesFrom(int square) const;
    bool isLegalMove(const Move &move) const;
    bool isInCheck(bool red) const;
    bool hasAnyLegalMove() const;
    QString validateForAnalysis() const;

    void applyMove(const Move &move);
    Position afterMove(const Move &move) const;

    QString moveNotation(const Move &move) const;
    static QString moveToUci(const Move &move);
    static Move moveFromUci(const QString &text);

    static int rowOf(int square) { return square / 9; }
    static int colOf(int square) { return square % 9; }
    static int square(int row, int col) { return row * 9 + col; }

    std::array<char, 90> board{};
    bool redTurn = true;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

private:
    QVector<Move> pseudoMoves(bool red) const;
    void addPseudoMoves(int from, QVector<Move> &moves) const;
    bool insidePalace(bool red, int row, int col) const;
};
