#include "boardrecognizer.h"
#include "boardwidget.h"
#include "piecedetector.h"

#include <QImageReader>
#include <QtTest>

#ifndef RECOGNITION_TEST_IMAGE
#define RECOGNITION_TEST_IMAGE "tests/data/board-popup.png"
#endif
#ifndef RECOGNITION_EDGE_CANNON_IMAGE
#define RECOGNITION_EDGE_CANNON_IMAGE "tests/data/tiantian-edge-cannon.png"
#endif
#ifndef RECOGNITION_MIDGAME_TYPES_IMAGE
#define RECOGNITION_MIDGAME_TYPES_IMAGE "tests/data/tiantian-midgame-types.png"
#endif
#ifndef RECOGNITION_EMPTY_EDGE_IMAGE
#define RECOGNITION_EMPTY_EDGE_IMAGE "tests/data/tiantian-empty-edge.png"
#endif
#ifndef RECOGNITION_WOODEN_STYLE_IMAGE
#define RECOGNITION_WOODEN_STYLE_IMAGE "tests/data/wooden-initial.png"
#endif
#ifndef RECOGNITION_LATEST_FAILURE_IMAGE
#define RECOGNITION_LATEST_FAILURE_IMAGE "tests/data/tiantian-latest-failure.png"
#endif
#ifndef RECOGNITION_RED_TOP_IMAGE
#define RECOGNITION_RED_TOP_IMAGE "tests/data/tiantian-red-top-black-turn.png"
#endif
#ifndef RECOGNITION_JJ_MIDGAME_1_IMAGE
#define RECOGNITION_JJ_MIDGAME_1_IMAGE "tests/data/jj-midgame-1.png"
#endif
#ifndef RECOGNITION_JJ_MIDGAME_2_IMAGE
#define RECOGNITION_JJ_MIDGAME_2_IMAGE "tests/data/jj-midgame-2.png"
#endif

class RecognitionTest : public QObject {
    Q_OBJECT
private slots:
    PieceDetection detectionAt(int row, int col, char piece, double confidence = 0.95)
    {
        const QPointF center(25.0 + col * 350.0 / 8.0, 25.0 + row * 400.0 / 9.0);
        return {QRectF(center - QPointF(15.0, 15.0), QSizeF(30.0, 30.0)), piece, confidence};
    }

    void mapsDetectionCentersToGridIncludingEdges()
    {
        const QVector<PieceDetection> detections = {
            detectionAt(0, 0, 'r'), detectionAt(0, 8, 'n'),
            detectionAt(9, 0, 'R'), detectionAt(9, 8, 'K'),
            {QRectF(0.0, 0.0, 400.0, 450.0), '0', 0.99}
        };
        const PieceGridResult grid = PieceDetector::mapToGrid(detections, QSize(400, 450));
        QCOMPARE(grid.position.piece(Position::square(0, 0)), 'r');
        QCOMPARE(grid.position.piece(Position::square(0, 8)), 'n');
        QCOMPARE(grid.position.piece(Position::square(9, 0)), 'R');
        QCOMPARE(grid.position.piece(Position::square(9, 8)), 'K');
        QCOMPARE(grid.unmappedPieces, 0);
        QVERIFY(grid.duplicateSquares.isEmpty());
        QVERIFY(grid.conflictingSquares.isEmpty());
    }

    void filtersLowConfidenceAndUnmappedDetections()
    {
        QVector<PieceDetection> detections = {detectionAt(2, 2, 'c', 0.34)};
        PieceGridResult grid = PieceDetector::mapToGrid(detections, QSize(400, 450));
        QCOMPARE(grid.ignoredLowConfidence, 1);
        QCOMPARE(grid.position.piece(Position::square(2, 2)), Piece::Empty);

        detections = {detectionAt(2, 2, 'c', 0.35),
                      {QRectF(-100.0, 10.0, 30.0, 30.0), 'r', 0.95},
                      {QRectF(31.0, 10.0, 30.0, 30.0), 'n', 0.95}};
        grid = PieceDetector::mapToGrid(detections, QSize(400, 450));
        QCOMPARE(grid.position.piece(Position::square(2, 2)), 'c');
        QCOMPARE(grid.unmappedPieces, 2);
    }

    void marksDuplicateAndConflictingDetections()
    {
        PieceGridResult grid = PieceDetector::mapToGrid(
            {detectionAt(0, 0, 'r', 0.90), detectionAt(0, 0, 'r', 0.80)}, QSize(400, 450));
        QVERIFY(grid.duplicateSquares.contains(0));
        QCOMPARE(grid.confidences[0], 0.60);

        grid = PieceDetector::mapToGrid(
            {detectionAt(0, 0, 'r', 0.90), detectionAt(0, 0, 'n', 0.95)}, QSize(400, 450));
        QVERIFY(grid.conflictingSquares.contains(0));
        QCOMPARE(grid.position.piece(0), 'n');
        QCOMPARE(grid.confidences[0], 0.0);
    }

    void recognizesProvidedScreenshot()
    {
        QImageReader reader(QStringLiteral(RECOGNITION_TEST_IMAGE));
        reader.setAutoTransform(true);
        const QImage image = reader.read().convertToFormat(QImage::Format_RGB888);
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));

        Position expected;
        QVERIFY(Position::fromFen(QStringLiteral(
            "2bak3r/r3RR3/2n1C4/p7p/2p1p1p2/9/P5n1P/2C3N2/4A4/2BAK1B2 b - - 0 1"), expected));

        BoardRecognizer recognizer;
        Position recognized;
        QVector<double> confidences;
        QString details;
        QString error;
        bool finished = false;
        connect(&recognizer, &BoardRecognizer::recognitionFinished, this,
                [&](const Position &position, const QString &text, const QVector<double> &scores,
                    const QVector<int> &) {
            recognized = position;
            details = text;
            confidences = scores;
            finished = true;
        });
        connect(&recognizer, &BoardRecognizer::recognitionFailed, this, [&](const QString &text) { error = text; });
        recognizer.recognize(image);
        QTRY_VERIFY_WITH_TIMEOUT(finished || !error.isEmpty(), 60000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        qDebug().noquote() << "Recognition:" << details;
        qDebug().noquote() << "FEN:" << recognized.toFen();
        QCOMPARE(confidences.size(), 90);

        int exactSquares = 0;
        int exactPieces = 0;
        int expectedPieces = 0;
        int recognizedPieces = 0;
        for (int square = 0; square < 90; ++square) {
            if (recognized.piece(square) != expected.piece(square))
                qDebug() << "Mismatch at" << Position::rowOf(square) << Position::colOf(square)
                         << "expected" << expected.piece(square) << "actual" << recognized.piece(square)
                         << "confidence" << confidences[square];
            if (recognized.piece(square) == expected.piece(square))
                ++exactSquares;
            if (expected.piece(square) != Piece::Empty) {
                ++expectedPieces;
                if (recognized.piece(square) == expected.piece(square))
                    ++exactPieces;
            }
            if (recognized.piece(square) != Piece::Empty)
                ++recognizedPieces;
        }
        qDebug() << "Exact occupied squares:" << exactPieces << "/" << expectedPieces
                 << "all squares:" << exactSquares << "/90" << "detected pieces:" << recognizedPieces;
        QCOMPARE(expectedPieces, 24);
        QCOMPARE(exactPieces, expectedPieces);
        QCOMPARE(exactSquares, 90);
        QCOMPARE(recognizedPieces, expectedPieces);
        QCOMPARE(recognized.piece(Position::square(0, 4)), Piece::make(false, Piece::King));
        QCOMPARE(recognized.piece(Position::square(9, 4)), Piece::King);
    }

    void recognizesColorCorrectedEdgeCannon()
    {
        QImageReader reader(QStringLiteral(RECOGNITION_EDGE_CANNON_IMAGE));
        reader.setAutoTransform(true);
        const QImage image = reader.read().convertToFormat(QImage::Format_RGB888);
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));

        BoardRecognizer recognizer;
        Position recognized;
        QVector<double> confidences;
        QVector<int> correctedSquares;
        QString error;
        bool finished = false;
        connect(&recognizer, &BoardRecognizer::recognitionFinished, this,
                [&](const Position &position, const QString &, const QVector<double> &scores,
                    const QVector<int> &corrected) {
            recognized = position;
            confidences = scores;
            correctedSquares = corrected;
            finished = true;
        });
        connect(&recognizer, &BoardRecognizer::recognitionFailed, this,
                [&](const QString &text) { error = text; });
        recognizer.recognize(image);
        QTRY_VERIFY_WITH_TIMEOUT(finished || !error.isEmpty(), 60000);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        Position expected;
        QVERIFY(Position::fromFen(QStringLiteral(
            "2bakab2/9/r1nc2n2/p1R1p1p1p/2p6/6P2/P1P1P1r1c/2N1CCNR1/4A4/2BAK1B2 w - - 0 1"), expected));
        QCOMPARE(recognized.toFen(), expected.toFen());
        QCOMPARE(recognized.piece(62), Piece::make(false, Piece::Cannon));
        QVERIFY(correctedSquares.contains(62));
        QVERIFY(confidences[62] >= 0.65);
    }

    void recognizesMidgameRookAndElephant()
    {
        QImageReader reader(QStringLiteral(RECOGNITION_MIDGAME_TYPES_IMAGE));
        reader.setAutoTransform(true);
        const QImage image = reader.read().convertToFormat(QImage::Format_RGB888);
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));

        BoardRecognizer recognizer;
        Position recognized;
        QVector<double> confidences;
        QVector<int> correctedSquares;
        QString error;
        bool finished = false;
        connect(&recognizer, &BoardRecognizer::recognitionFinished, this,
                [&](const Position &position, const QString &, const QVector<double> &scores,
                    const QVector<int> &corrected) {
            recognized = position;
            confidences = scores;
            correctedSquares = corrected;
            finished = true;
        });
        connect(&recognizer, &BoardRecognizer::recognitionFailed, this,
                [&](const QString &text) { error = text; });
        recognizer.recognize(image);
        QTRY_VERIFY_WITH_TIMEOUT(finished || !error.isEmpty(), 60000);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        Position expected;
        QVERIFY(Position::fromFen(QStringLiteral(
            "3ak1b2/4a4/r2cb1n2/C1R5p/6p2/8r/P3P1c2/2N1C1NRB/4A4/2BAK4 w - - 0 1"), expected));
        QCOMPARE(recognized.toFen(), expected.toFen());
        QCOMPARE(recognized.piece(53), Piece::make(false, Piece::Rook));
        QCOMPARE(recognized.piece(71), Piece::Elephant);
        QVERIFY(correctedSquares.contains(53));
        QVERIFY(correctedSquares.contains(71));
        QVERIFY(confidences[53] >= 0.65);
        QVERIFY(confidences[71] >= 0.65);
    }

    void rejectsPhantomRookOnEmptyEdge()
    {
        QImageReader reader(QStringLiteral(RECOGNITION_EMPTY_EDGE_IMAGE));
        reader.setAutoTransform(true);
        const QImage image = reader.read().convertToFormat(QImage::Format_RGB888);
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));

        BoardRecognizer recognizer;
        Position recognized;
        QVector<double> confidences;
        QVector<int> correctedSquares;
        QString error;
        bool finished = false;
        connect(&recognizer, &BoardRecognizer::recognitionFinished, this,
                [&](const Position &position, const QString &, const QVector<double> &scores,
                    const QVector<int> &corrected) {
            recognized = position;
            confidences = scores;
            correctedSquares = corrected;
            finished = true;
        });
        connect(&recognizer, &BoardRecognizer::recognitionFailed, this,
                [&](const QString &text) { error = text; });
        recognizer.recognize(image);
        QTRY_VERIFY_WITH_TIMEOUT(finished || !error.isEmpty(), 60000);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        Position expected;
        QVERIFY(Position::fromFen(QStringLiteral(
            "2bakabr1/9/n1c3nc1/p1p1p2Rp/6p2/2P6/r3P1P1P/B2CC1N1B/3NA4/1R1AK4 w - - 0 1"), expected));
        QCOMPARE(recognized.toFen(), expected.toFen());
        QCOMPARE(recognized.piece(89), Piece::Empty);
        QVERIFY(correctedSquares.contains(89));
        QVERIFY(confidences[89] >= 0.65);
    }

    void recognizesSuppliedWoodenStyle()
    {
        QImageReader reader(QStringLiteral(RECOGNITION_WOODEN_STYLE_IMAGE));
        reader.setAutoTransform(true);
        const QImage image = reader.read().convertToFormat(QImage::Format_RGB888);
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));

        BoardRecognizer recognizer;
        Position recognized;
        QString error;
        bool finished = false;
        connect(&recognizer, &BoardRecognizer::recognitionFinished, this,
                [&](const Position &position, const QString &, const QVector<double> &,
                    const QVector<int> &) {
            recognized = position;
            finished = true;
        });
        connect(&recognizer, &BoardRecognizer::recognitionFailed, this,
                [&](const QString &text) { error = text; });
        recognizer.recognize(image);
        QTRY_VERIFY_WITH_TIMEOUT(finished || !error.isEmpty(), 60000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(recognized.toFen(), Position::initial().toFen());
    }

    void recognizesLatestRealFailureSample()
    {
        QImageReader reader(QStringLiteral(RECOGNITION_LATEST_FAILURE_IMAGE));
        reader.setAutoTransform(true);
        const QImage image = reader.read().convertToFormat(QImage::Format_RGB888);
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));

        BoardRecognizer recognizer;
        Position recognized;
        QVector<double> confidences;
        QString error;
        bool finished = false;
        connect(&recognizer, &BoardRecognizer::recognitionFinished, this,
                [&](const Position &position, const QString &, const QVector<double> &scores,
                    const QVector<int> &) {
            recognized = position;
            confidences = scores;
            finished = true;
        });
        connect(&recognizer, &BoardRecognizer::recognitionFailed, this,
                [&](const QString &text) { error = text; });
        recognizer.recognize(image);
        QTRY_VERIFY_WITH_TIMEOUT(finished || !error.isEmpty(), 60000);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        Position expected;
        QVERIFY(Position::fromFen(QStringLiteral(
            "r1bakabn1/7c1/2n4cr/p1p1p3p/6p2/2P6/P3P1P1P/1CN1C1N2/9/R1BAKABR1 w - - 0 1"), expected));
        QCOMPARE(recognized.toFen(), expected.toFen());
        QCOMPARE(confidences.size(), 90);
        for (int square = 0; square < 90; ++square)
            QVERIFY2(confidences[square] >= 0.65,
                qPrintable(QStringLiteral("square %1 confidence %2").arg(square).arg(confidences[square])));
    }

    void recognizesJjMidgameEdgeRooks()
    {
        const struct {
            const char *imagePath;
            const char *fen;
        } cases[] = {
            {RECOGNITION_JJ_MIDGAME_1_IMAGE,
             "3ak1b2/4a4/1cn1b1c1n/r3p3p/p1p6/3R1NP2/P1P1P3P/2N1C4/9/2BAKABR1 w - - 0 1"},
            {RECOGNITION_JJ_MIDGAME_2_IMAGE,
             "3ak1b2/1c2a4/4b1cRn/r3n4/p7p/2R3P2/P3P3P/2N1C4/9/2BAKAB2 w - - 0 1"}
        };
        for (const auto &testCase : cases) {
            QImageReader reader(QString::fromUtf8(testCase.imagePath));
            reader.setAutoTransform(true);
            const QImage image = reader.read().convertToFormat(QImage::Format_RGB888);
            QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));

            BoardRecognizer recognizer;
            Position recognized;
            QVector<double> confidences;
            QString error;
            bool finished = false;
            connect(&recognizer, &BoardRecognizer::recognitionFinished, this,
                    [&](const Position &position, const QString &, const QVector<double> &scores,
                        const QVector<int> &) {
                recognized = position;
                confidences = scores;
                finished = true;
            });
            connect(&recognizer, &BoardRecognizer::recognitionFailed, this,
                    [&](const QString &text) { error = text; });
            recognizer.recognize(image, BoardOrientation::RedTop);
            QTRY_VERIFY_WITH_TIMEOUT(finished || !error.isEmpty(), 60000);
            QVERIFY2(error.isEmpty(), qPrintable(error));

            Position expected;
            QVERIFY(Position::fromFen(QString::fromUtf8(testCase.fen), expected));
            QCOMPARE(recognized.toFen(), expected.toFen());
            QCOMPARE(recognized.piece(Position::square(3, 0)), Piece::make(false, Piece::Rook));
            QCOMPARE(confidences.size(), 90);
            for (int square = 0; square < 90; ++square) {
                QVERIFY2(confidences[square] >= 0.65,
                    qPrintable(QStringLiteral("%1 square %2 confidence %3")
                        .arg(QString::fromUtf8(testCase.imagePath)).arg(square).arg(confidences[square])));
            }
        }
    }

    void recognizesRedTopInitialPositionWithBlackToMove()
    {
        QImageReader reader(QStringLiteral(RECOGNITION_RED_TOP_IMAGE));
        reader.setAutoTransform(true);
        const QImage image = reader.read().convertToFormat(QImage::Format_RGB888);
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));

        Position expected;
        QVERIFY(Position::fromFen(QStringLiteral(
            "rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C2C4/9/RNBAKABNR w - - 0 1"), expected));

        for (const BoardOrientation orientation : {BoardOrientation::RedTop, BoardOrientation::Auto}) {
            BoardRecognizer recognizer;
            Position recognized;
            QVector<double> confidences;
            QPolygonF corners;
            QString details;
            QString error;
            bool finished = false;
            connect(&recognizer, &BoardRecognizer::boardLocated, this,
                    [&](const QPolygonF &located) { corners = located; });
            connect(&recognizer, &BoardRecognizer::recognitionFinished, this,
                    [&](const Position &position, const QString &text, const QVector<double> &scores,
                        const QVector<int> &) {
                recognized = position;
                details = text;
                confidences = scores;
                finished = true;
            });
            connect(&recognizer, &BoardRecognizer::recognitionFailed, this,
                    [&](const QString &text) { error = text; });
            recognizer.recognize(image, orientation);
            QTRY_VERIFY_WITH_TIMEOUT(finished || !error.isEmpty(), 60000);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            qDebug().noquote() << "Red-top recognition:" << details;
            qDebug().noquote() << "FEN:" << recognized.toFen();
            QCOMPARE(recognized.toFen(), expected.toFen());
            QCOMPARE(confidences.size(), 90);
            for (int square = 0; square < 90; ++square) {
                QVERIFY2(confidences[square] >= 0.65,
                    qPrintable(QStringLiteral("square %1 confidence %2")
                        .arg(square).arg(confidences[square])));
            }
            QCOMPARE(corners.size(), 4);
            QVERIFY(corners[0].y() < corners[2].y());
            QVERIFY(corners[1].y() < corners[3].y());
            QVERIFY(corners[0].x() < corners[1].x());
            QVERIFY(corners[2].x() < corners[3].x());

            recognized.redTurn = false;
            QVERIFY2(recognized.validateForAnalysis().isEmpty(),
                     qPrintable(recognized.validateForAnalysis()));
            const Move blackFirstMove = Position::moveFromUci(QStringLiteral("b9c7"));
            QVERIFY(recognized.isLegalMove(blackFirstMove));
        }
    }

    void recognizesRenderedInitialBoard()
    {
        BoardWidget board;
        board.resize(720, 800);
        board.setPosition(Position::initial());
        board.show();
        QTest::qWait(80);
        const QImage image = board.grab().toImage().convertToFormat(QImage::Format_RGB888);
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QCoreApplication::applicationDirPath() + QStringLiteral("/recognition-rendered-input.png")));

        BoardRecognizer recognizer;
        Position recognized;
        QString error;
        QString details;
        bool finished = false;
        connect(&recognizer, &BoardRecognizer::recognitionFinished, this,
                [&](const Position &position, const QString &text, const QVector<double> &,
                    const QVector<int> &) {
            recognized = position;
            details = text;
            finished = true;
        });
        connect(&recognizer, &BoardRecognizer::recognitionFailed, this, [&](const QString &text) { error = text; });
        recognizer.recognize(image);
        QTRY_VERIFY_WITH_TIMEOUT(finished || !error.isEmpty(), 60000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        qDebug().noquote() << "Rendered board:" << details;
        qDebug().noquote() << "FEN:" << recognized.toFen();
        QCOMPARE(recognized.toFen().split(QLatin1Char(' ')).first(),
                 Position::initial().toFen().split(QLatin1Char(' ')).first());
        board.close();
    }
};

QTEST_MAIN(RecognitionTest)
#include "recognition_test.moc"
