#include "autoplaylogic.h"

#include <QtTest>

class AutoplayLogicTest : public QObject {
    Q_OBJECT
private slots:
    void requiresTwoConsecutiveFrames()
    {
        BoardObservationStability stability;
        const Position initial = Position::initial();
        QVERIFY(!stability.observe(initial));
        QCOMPARE(stability.consecutiveFrames(), 1);
        QVERIFY(stability.observe(initial));
        QCOMPARE(stability.consecutiveFrames(), 2);
        QVERIFY(stability.observe(initial));
    }

    void unreliableFrameBreaksStability()
    {
        BoardObservationStability stability;
        const Position initial = Position::initial();
        QVERIFY(!stability.observe(initial));
        QVERIFY(stability.observe(initial));
        stability.reset();
        QCOMPARE(stability.consecutiveFrames(), 0);
        QVERIFY(!stability.observe(initial));
        QVERIFY(stability.observe(initial));
    }

    void changedFrameRestartsConfirmation()
    {
        const Position initial = Position::initial();
        const Move move = Position::moveFromUci(QStringLiteral("a0a1"));
        QVERIFY(initial.isLegalMove(move));
        const Position changed = initial.afterMove(move);
        BoardObservationStability stability;
        QVERIFY(!stability.observe(initial));
        QVERIFY(!stability.observe(changed));
        QCOMPARE(stability.consecutiveFrames(), 1);
        QVERIFY(!stability.observe(initial));
        QVERIFY(!stability.observe(changed));
        QVERIFY(stability.observe(changed));
    }

    void acceptsExactlyOneLegalMove()
    {
        const Position before = Position::initial();
        const Move expected = Position::moveFromUci(QStringLiteral("a0a1"));
        QVERIFY(before.isLegalMove(expected));
        const Position observed = before.afterMove(expected);
        Move actual;
        Position after;
        QVERIFY(findSingleLegalTransition(before, observed, &actual, &after));
        QCOMPARE(Position::moveToUci(actual), QStringLiteral("a0a1"));
        QCOMPARE(after.board, observed.board);
        QCOMPARE(after.redTurn, !before.redTurn);
    }

    void mapsRedTopClicksInOriginalImageCoordinates()
    {
        const QPolygonF corners = {QPointF(10.0, 20.0), QPointF(810.0, 20.0),
                                   QPointF(10.0, 920.0), QPointF(810.0, 920.0)};
        QCOMPARE(boardImagePointForUci(QStringLiteral("a9"), corners, true),
                 QPointF(10.0, 20.0));
        QCOMPARE(boardImagePointForUci(QStringLiteral("a9"), corners, false),
                 QPointF(810.0, 920.0));
        QCOMPARE(boardImagePointForUci(QStringLiteral("h0"), corners, false),
                 QPointF(110.0, 20.0));
        QCOMPARE(boardImagePointForUci(QStringLiteral("b9c7").left(2), corners, false),
                 QPointF(710.0, 920.0));
        QCOMPARE(boardImagePointForUci(QStringLiteral("b9c7").mid(2, 2), corners, false),
                 QPointF(610.0, 720.0));
    }

    void rejectsTwoMovesAndUnrelatedChanges()
    {
        const Position before = Position::initial();
        const Move redMove = Position::moveFromUci(QStringLiteral("a0a1"));
        const Position afterRed = before.afterMove(redMove);
        const Move blackMove = Position::moveFromUci(QStringLiteral("a9a8"));
        QVERIFY(afterRed.isLegalMove(blackMove));
        const Position afterBlack = afterRed.afterMove(blackMove);
        QVERIFY(!findSingleLegalTransition(before, afterBlack));

        Position arbitrary = before;
        arbitrary.setPiece(Position::square(4, 4), Piece::Rook);
        QVERIFY(!findSingleLegalTransition(before, arbitrary));
        QVERIFY(!findSingleLegalTransition(before, before));
    }
};

QTEST_APPLESS_MAIN(AutoplayLogicTest)
#include "autoplay_logic_test.moc"
