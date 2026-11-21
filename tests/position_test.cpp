#include "position.h"

#include <QtTest>

class PositionTest : public QObject {
    Q_OBJECT

private:
    static quint64 perft(const Position &position, int depth)
    {
        if (depth == 0)
            return 1;
        quint64 nodes = 0;
        const QVector<Move> moves = position.legalMoves();
        for (const Move &move : moves)
            nodes += perft(position.afterMove(move), depth - 1);
        return nodes;
    }

private slots:
    void initialPosition()
    {
        const Position position = Position::initial();
        QCOMPARE(position.legalMoves().size(), 44);
        QCOMPARE(position.toFen(), QStringLiteral("rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1"));
        QVERIFY(!position.isInCheck(true));
        QVERIFY(!position.isInCheck(false));
        QVERIFY(position.validateForAnalysis().isEmpty());
    }

    void coordinateConversion()
    {
        const Move move = Position::moveFromUci(QStringLiteral("h2e2"));
        QCOMPARE(move.from, Position::square(7, 7));
        QCOMPARE(move.to, Position::square(7, 4));
        QCOMPARE(Position::moveToUci(move), QStringLiteral("h2e2"));
        QCOMPARE(Position::initial().moveNotation(move), QStringLiteral("炮二平五"));
        Position after = Position::initial().afterMove(move);
        QCOMPARE(after.toFen().section(QLatin1Char(' '), 1, 1), QStringLiteral("b"));
        const Move horse = Position::moveFromUci(QStringLiteral("b9c7"));
        QCOMPARE(after.moveNotation(horse), QStringLiteral("马8进7"));
    }

    void fenRoundTrip()
    {
        Position first = Position::initial().afterMove(Position::moveFromUci(QStringLiteral("h2e2")));
        first.applyMove(Position::moveFromUci(QStringLiteral("b9c7")));
        Position second;
        QString error;
        QVERIFY(Position::fromFen(first.toFen(), second, &error));
        QCOMPARE(second.toFen(), first.toFen());
        QCOMPARE(second.legalMoves().size(), first.legalMoves().size());
        QVERIFY(!Position::fromFen(QStringLiteral("9/9 w - - 0 1"), second, &error));
        QVERIFY(!error.isEmpty());
    }

    void horseLeg()
    {
        Position position = Position::empty();
        position.setPiece(Position::square(0, 4), Piece::make(false, Piece::King));
        position.setPiece(Position::square(9, 4), Piece::King);
        position.setPiece(Position::square(5, 4), Piece::Horse);
        position.setPiece(Position::square(4, 4), Piece::Pawn);
        const QVector<Move> moves = position.legalMovesFrom(Position::square(5, 4));
        QCOMPARE(moves.size(), 6);
        QVERIFY(!moves.contains(Move{Position::square(5, 4), Position::square(3, 3)}));
        QVERIFY(!moves.contains(Move{Position::square(5, 4), Position::square(3, 5)}));
    }

    void pawnRiver()
    {
        Position position = Position::empty();
        position.setPiece(Position::square(0, 3), Piece::make(false, Piece::King));
        position.setPiece(Position::square(9, 5), Piece::King);
        position.setPiece(Position::square(6, 0), Piece::Pawn);
        position.setPiece(Position::square(4, 8), Piece::Pawn);
        position.setPiece(Position::square(4, 6), Piece::Pawn);
        QCOMPARE(position.legalMovesFrom(Position::square(6, 0)).size(), 1);
        QCOMPARE(position.legalMovesFrom(Position::square(4, 8)).size(), 2);
        QCOMPARE(position.legalMovesFrom(Position::square(4, 6)).size(), 3);
    }

    void flyingGenerals()
    {
        Position position = Position::empty();
        position.setPiece(Position::square(0, 4), Piece::make(false, Piece::King));
        position.setPiece(Position::square(9, 4), Piece::King);
        QVERIFY(position.isInCheck(true));
        QVERIFY(position.isInCheck(false));
        QVERIFY(!position.validateForAnalysis().isEmpty());

        position.setPiece(Position::square(8, 4), Piece::Rook);
        QVERIFY(!position.isInCheck(true));
        const Move expose = {Position::square(8, 4), Position::square(8, 3)};
        QVERIFY(!position.legalMoves().contains(expose));
    }

    void cannonScreen()
    {
        Position position = Position::empty();
        position.setPiece(Position::square(0, 3), Piece::make(false, Piece::King));
        position.setPiece(Position::square(9, 5), Piece::King);
        const int cannon = Position::square(5, 0);
        position.setPiece(cannon, Piece::Cannon);
        position.setPiece(Position::square(3, 0), Piece::Pawn);
        position.setPiece(Position::square(1, 0), Piece::make(false, Piece::Pawn));
        position.setPiece(Position::square(0, 0), Piece::make(false, Piece::Rook));
        const QVector<Move> moves = position.legalMovesFrom(cannon);
        QVERIFY(moves.contains(Move{cannon, Position::square(1, 0)}));
        QVERIFY(!moves.contains(Move{cannon, Position::square(0, 0)}));
        QVERIFY(!moves.contains(Move{cannon, Position::square(3, 0)}));
    }

    void standardPerft()
    {
        const Position position = Position::initial();
        QCOMPARE(perft(position, 1), quint64(44));
        QCOMPARE(perft(position, 2), quint64(1920));
        QCOMPARE(perft(position, 3), quint64(79666));
    }
};

QTEST_APPLESS_MAIN(PositionTest)
#include "position_test.moc"
