#include "mainwindow.h"
#include "boardrecognizer.h"
#include "boardwidget.h"
#include "pikafishengine.h"
#include "recognitiondialog.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QtTest>

#ifndef ENGINE_TEST_PATH
#define ENGINE_TEST_PATH "Pikafish"
#endif
#ifndef UI_SCREENSHOT_PATH
#define UI_SCREENSHOT_PATH "xiangqi-analyzer.png"
#endif

namespace {
QPoint boardPoint(const BoardWidget *board, int row, int col)
{
    const double horizontalMargin = qMax(28.0, board->width() * 0.055);
    const double verticalMargin = qMax(28.0, board->height() * 0.035);
    const double cell = qMax(30.0, qMin((board->width() - 2 * horizontalMargin) / 8.0,
                                       (board->height() - 2 * verticalMargin) / 9.0));
    const QPointF origin((board->width() - 8 * cell) / 2.0,
                         (board->height() - 9 * cell) / 2.0);
    return (origin + QPointF(col * cell, row * cell)).toPoint();
}
}

class UiSmokeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("XiangqiAnalyzerTests"));
        QCoreApplication::setApplicationName(QStringLiteral("UiSmokeTest"));
        QSettings().clear();
    }

    void analyzesAndRenders()
    {
        MainWindow window;
        qDebug() << "UI: constructed";
        window.resize(1440, 940);
        window.show();
        qDebug() << "UI: shown";
        QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
        qDebug() << "UI: exposed";
        int mainPalettePieces = 0;
        for (QToolButton *button : window.findChildren<QToolButton *>()) {
            const QString piece = button->property("piece").toString();
            if (piece.size() != 1 || piece == QStringLiteral("."))
                continue;
            ++mainPalettePieces;
            QVERIFY(!button->icon().isNull());
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
        }
        QCOMPARE(mainPalettePieces, 14);
        auto *engine = window.findChild<PikafishEngine *>();
        QVERIFY(engine);
        engine->setEnginePath(QStringLiteral(ENGINE_TEST_PATH));
        engine->setMoveTime(1000);
        auto *analyze = window.findChild<QPushButton *>(QStringLiteral("primaryButton"));
        QVERIFY(analyze);
        connect(analyze, &QPushButton::clicked, this, []() { qDebug() << "UI: analyze signal"; });
        connect(engine, &PikafishEngine::statusChanged, this, [](const QString &status, bool thinking) {
            qDebug() << "UI: engine status" << status << thinking;
        });
        connect(engine, &PikafishEngine::errorOccurred, this, [](const QString &message) {
            qDebug() << "UI: engine error" << message;
        });
        const auto tables = window.findChildren<QTableWidget *>();
        QVERIFY(tables.size() >= 2);
        QTableWidget *candidates = nullptr;
        for (QTableWidget *table : tables) {
            if (table->columnCount() == 4 && table->horizontalHeaderItem(1)
                && table->horizontalHeaderItem(1)->text() == QStringLiteral("着法")) {
                candidates = table;
                break;
            }
        }
        QVERIFY(candidates);
        QTest::mouseClick(analyze, Qt::LeftButton);
        qDebug() << "UI: analysis clicked";
        QTRY_VERIFY_WITH_TIMEOUT(candidates->rowCount() >= 3, 20000);
        qDebug() << "UI: candidates received";
        QTRY_VERIFY_WITH_TIMEOUT(!engine->isThinking(), 20000);
        qDebug() << "UI: analysis finished";
        auto *scoreLabel = window.findChild<QLabel *>(QStringLiteral("scoreLabel"));
        QVERIFY(scoreLabel);
        bool integerScore = false;
        scoreLabel->text().toInt(&integerScore);
        QVERIFY2(integerScore, qPrintable(QStringLiteral("评分未按原版整数口径显示：%1")
            .arg(scoreLabel->text())));
        QVERIFY(!scoreLabel->text().contains(QLatin1Char('.')));
        QStringList visibleText = {window.windowTitle()};
        for (QAction *action : window.findChildren<QAction *>())
            visibleText << action->text() << action->toolTip();
        for (QLabel *label : window.findChildren<QLabel *>())
            visibleText << label->text() << label->toolTip();
        for (QAbstractButton *button : window.findChildren<QAbstractButton *>())
            visibleText << button->text() << button->toolTip();
        for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
            for (int i = 0; i < tabs->count(); ++i)
                visibleText << tabs->tabText(i);
        for (const QString &text : std::as_const(visibleText)) {
            QVERIFY2(!text.contains(QStringLiteral("引擎"))
                         && !text.contains(QStringLiteral("皮卡鱼"))
                         && !text.contains(QStringLiteral("Pikafish"), Qt::CaseInsensitive),
                     qPrintable(QStringLiteral("旧名称仍显示在界面中：%1").arg(text)));
        }
        auto *variations = window.findChild<QTextBrowser *>();
        QVERIFY(variations);
        QVERIFY(variations->toPlainText().contains(QStringLiteral("红分")));
        QVERIFY(!variations->toPlainText().contains(QStringLiteral("UCI")));
        QVERIFY(variations->toPlainText().contains(QStringLiteral("D")));
        QVERIFY(variations->toHtml().contains(QStringLiteral("pv://0/0")));
        QVERIFY(candidates->height() <= 190);
        QVERIFY(variations->height() >= 280);
        QTest::qWait(250);
        const QPixmap screenshot = window.grab();
        QVERIFY(!screenshot.isNull());
        QVERIFY2(screenshot.save(QStringLiteral(UI_SCREENSHOT_PATH)), "Could not save UI smoke-test screenshot");
        qDebug() << "UI: screenshot saved";
        auto *board = window.findChild<BoardWidget *>();
        QVERIFY(board);
        auto *autoplayButton = window.findChild<QPushButton *>(QStringLiteral("autoplayButton"));
        QVERIFY(autoplayButton);
        QCOMPARE(autoplayButton->text(), QStringLiteral("自动对弈"));
        QVERIFY(QMetaObject::invokeMethod(variations, "anchorClicked",
            Q_ARG(QUrl, QUrl(QStringLiteral("pv://0/0")))));
        auto *preview = window.findChild<QDialog *>(QStringLiteral("variationPreview"));
        QVERIFY(preview);
        auto *previewBoard = preview->findChild<BoardWidget *>();
        QVERIFY(previewBoard);
        QVERIFY(previewBoard->position().toFen() != board->position().toFen());
        QTest::qWait(80);
        QVERIFY(preview->grab().save(QStringLiteral(UI_SCREENSHOT_PATH).replace(QStringLiteral(".png"), QStringLiteral("-preview.png"))));
        QPushButton *next = nullptr;
        for (QPushButton *button : preview->findChildren<QPushButton *>())
            if (button->text().startsWith(QStringLiteral("下一步")))
                next = button;
        QVERIFY(next);
        const QString firstPreviewFen = previewBoard->position().toFen();
        QTest::mouseClick(next, Qt::LeftButton);
        QVERIFY(previewBoard->position().toFen() != firstPreviewFen);
        preview->close();
        board->setHintMove(Position::moveFromUci(QStringLiteral("b9c7")));
        QTest::qWait(60);
        QVERIFY(board->grab().save(QStringLiteral(UI_SCREENSHOT_PATH).replace(QStringLiteral(".png"), QStringLiteral("-diagonal.png"))));
        board->setHintMove(Position::moveFromUci(QStringLiteral("a3a4")));
        QTest::qWait(60);
        QVERIFY(board->grab().save(QStringLiteral(UI_SCREENSHOT_PATH).replace(QStringLiteral(".png"), QStringLiteral("-short.png"))));
        QPushButton *engineMove = nullptr;
        const auto buttons = window.findChildren<QPushButton *>();
        for (QPushButton *button : buttons)
            if (button->text() == QStringLiteral("电脑走棋"))
                engineMove = button;
        QVERIFY(engineMove);
        const QString beforeMove = board->position().toFen();
        QTest::mouseClick(engineMove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(board->position().toFen() != beforeMove, 2000);
        QVERIFY(!engine->isThinking());

        QVERIFY(QMetaObject::invokeMethod(&window, "recognizeBoard", Qt::DirectConnection));
        QPointer<RecognitionDialog> captureDialog =
            window.findChild<RecognitionDialog *>(QStringLiteral("recognitionDialog"));
        QVERIFY(captureDialog);
        QTRY_VERIFY_WITH_TIMEOUT(captureDialog->isVisible(), 2000);
        captureDialog->hide();
        QTest::qWait(100);
        QVERIFY(captureDialog);
        captureDialog->show();
        QTRY_VERIFY_WITH_TIMEOUT(captureDialog->isVisible(), 2000);
        captureDialog->close();
        QTRY_VERIFY_WITH_TIMEOUT(captureDialog.isNull(), 2000);
        window.close();
        qDebug() << "UI: closed";
    }

    void savesSelectedEnginePath()
    {
        MainWindow window;
        QTimer::singleShot(0, &window, [&window]() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *pathEdit = dialog->findChild<QLineEdit *>(QStringLiteral("enginePathEdit"));
            if (pathEdit)
                pathEdit->setText(QStringLiteral(ENGINE_TEST_PATH));
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (buttons)
                buttons->button(QDialogButtonBox::Save)->click();
            else
                dialog->reject();
        });
        QVERIFY(QMetaObject::invokeMethod(&window, "showSettings", Qt::DirectConnection));
        auto *engine = window.findChild<PikafishEngine *>();
        QVERIFY(engine);
        QCOMPARE(engine->enginePath(), QStringLiteral(ENGINE_TEST_PATH));
        QCOMPARE(QSettings().value(QStringLiteral("engine/path")).toString(),
                 QStringLiteral(ENGINE_TEST_PATH));
    }

    void middleButtonDrag()
    {
        MainWindow window;
        window.resize(1440, 940);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
        QVERIFY(QMetaObject::invokeMethod(&window, "toggleEditMode", Qt::DirectConnection));
        QTest::qWait(100);
        auto *board = window.findChild<BoardWidget *>();
        QVERIFY(board);
        QVERIFY(board->editMode());

        const int source = Position::square(9, 0);
        const int emptyTarget = Position::square(5, 0);
        const char rook = board->position().piece(source);
        QTest::mousePress(board, Qt::MiddleButton, Qt::NoModifier, boardPoint(board, 9, 0));
        QTest::mouseMove(board, boardPoint(board, 5, 0));
        QTest::mouseRelease(board, Qt::MiddleButton, Qt::NoModifier, boardPoint(board, 5, 0));
        QCOMPARE(board->position().piece(source), Piece::Empty);
        QCOMPARE(board->position().piece(emptyTarget), rook);

        const int occupiedTarget = Position::square(0, 0);
        const char occupyingRook = board->position().piece(occupiedTarget);
        QTest::mousePress(board, Qt::MiddleButton, Qt::NoModifier, boardPoint(board, 5, 0));
        QTest::mouseMove(board, boardPoint(board, 0, 0));
        QTest::mouseRelease(board, Qt::MiddleButton, Qt::NoModifier, boardPoint(board, 0, 0));
        QCOMPARE(board->position().piece(emptyTarget), rook);
        QCOMPARE(board->position().piece(occupiedTarget), occupyingRook);
        window.close();
    }

    void imageDragAndDrop()
    {
        RecognitionDialog dialog;
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog, 5000));
        BoardWidget source;
        source.resize(540, 610);
        source.setPosition(Position::initial());
        source.show();
        QTest::qWait(80);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("dropped-board.png"));
        QVERIFY(source.grab().toImage().convertToFormat(QImage::Format_RGB888).save(path));

        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&dialog, &enter);
        QVERIFY(enter.isAccepted());

        auto *recognizer = dialog.findChild<BoardRecognizer *>();
        QVERIFY(recognizer);
        QSignalSpy finishedSpy(recognizer, &BoardRecognizer::recognitionFinished);
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&dialog, &drop);
        QVERIFY(drop.isAccepted());
        QVERIFY(finishedSpy.wait(15000));
        auto *board = dialog.findChild<BoardWidget *>(QStringLiteral("recognitionBoard"));
        QVERIFY(board);
        QCOMPARE(board->position().toFen(), Position::initial().toFen());
        source.close();
    }

    void recognitionCorrection()
    {
        RecognitionDialog dialog;
        dialog.resize(1180, 850);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog, 5000));
        int recognitionPalettePieces = 0;
        for (QToolButton *button : dialog.findChildren<QToolButton *>()) {
            const QString piece = button->property("piece").toString();
            if (piece.size() != 1 || piece == QStringLiteral("."))
                continue;
            ++recognitionPalettePieces;
            QVERIFY(!button->icon().isNull());
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
        }
        QCOMPARE(recognitionPalettePieces, 14);
        BoardWidget source;
        source.resize(540, 610);
        source.setPosition(Position::initial());
        source.show();
        QTest::qWait(80);
        dialog.loadImage(source.grab().toImage(), false);
        auto *recognizer = dialog.findChild<BoardRecognizer *>();
        QVERIFY(recognizer);
        recognizer->recognitionFinished(Position::initial(), QStringLiteral("界面测试局面"),
                                        QVector<double>(90, 1.0), {});
        auto *board = dialog.findChild<BoardWidget *>(QStringLiteral("recognitionBoard"));
        QVERIFY(board);
        QCOMPARE(board->position().toFen(), Position::initial().toFen());
        QPushButton *rotate = nullptr;
        for (QPushButton *button : dialog.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("旋转 180°"))
                rotate = button;
        QVERIFY(rotate);
        QTest::mouseClick(rotate, Qt::LeftButton);
        QVERIFY(board->flipped());
        QCOMPARE(board->position().toFen(), Position::initial().toFen());
        QCOMPARE(board->position().piece(Position::square(0, 0)), Piece::make(false, Piece::Rook));
        QTest::mouseClick(rotate, Qt::LeftButton);
        QVERIFY(!board->flipped());
        QCOMPARE(board->position().toFen(), Position::initial().toFen());
        // Applying while visually flipped must still export the original position.
        QTest::mouseClick(rotate, Qt::LeftButton);
        QVERIFY(board->flipped());
        QCOMPARE(board->position().toFen(), Position::initial().toFen());
        QTest::qWait(100);
        QVERIFY(dialog.grab().save(QStringLiteral(UI_SCREENSHOT_PATH).replace(QStringLiteral(".png"), QStringLiteral("-recognition.png"))));
        bool accepted = false;
        connect(&dialog, &RecognitionDialog::positionAccepted, this, [&accepted](const Position &position) {
            accepted = position.toFen() == Position::initial().toFen();
        });
        auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("primaryButton"));
        QVERIFY(apply);
        QTest::mouseClick(apply, Qt::LeftButton);
        QVERIFY(accepted);
        source.close();
    }
};

QTEST_MAIN(UiSmokeTest)
#include "ui_smoke_test.moc"
