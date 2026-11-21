#include "recognitiondialog.h"

#include "boardrecognizer.h"
#include "boardwidget.h"

#include <algorithm>

#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

RecognitionDialog::RecognitionDialog(QWidget *parent)
    : QDialog(parent)
    , m_position(Position::empty())
    , m_recognizer(new BoardRecognizer(this))
{
    setWindowTitle(QStringLiteral("图片识别棋盘 · 本地离线识别"));
    setObjectName(QStringLiteral("recognitionDialog"));
    setAcceptDrops(true);
    resize(1180, 850);
    setMinimumSize(980, 720);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 14, 16, 14);
    outerLayout->setSpacing(10);
    auto *sourceButtons = new QHBoxLayout;
    m_captureButton = new QPushButton(QStringLiteral("系统截图"), this);
    m_captureButton->setToolTip(QStringLiteral("调用 macOS 区域截图，截图会复制到剪贴板并自动载入"));
    m_importButton = new QPushButton(QStringLiteral("导入图片"), this);
    m_recognizeButton = new QPushButton(QStringLiteral("重新识别"), this);
    m_recognizeButton->setEnabled(false);
    sourceButtons->addWidget(m_captureButton);
    sourceButtons->addWidget(m_importButton);
    sourceButtons->addWidget(m_recognizeButton);
    sourceButtons->addStretch();
    auto *modelLabel = new QLabel(BoardRecognizer::modelDescription(), this);
    modelLabel->setProperty("muted", true);
    sourceButtons->addWidget(modelLabel);
    outerLayout->addLayout(sourceButtons);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    outerLayout->addWidget(splitter, 1);

    auto *sourcePanel = new QFrame(splitter);
    sourcePanel->setProperty("card", true);
    auto *sourceLayout = new QVBoxLayout(sourcePanel);
    sourceLayout->setContentsMargins(12, 12, 12, 12);
    auto *sourceTitle = new QLabel(QStringLiteral("原始图片"), sourcePanel);
    sourceTitle->setObjectName(QStringLiteral("sectionTitle"));
    sourceLayout->addWidget(sourceTitle);
    m_sourceLabel = new QLabel(QStringLiteral("将图片直接拖到这里，或选择“系统截图”“导入图片”\n支持常见象棋软件、网页和棋盘照片"), sourcePanel);
    m_sourceLabel->setObjectName(QStringLiteral("recognitionSourceImage"));
    m_sourceLabel->setAlignment(Qt::AlignCenter);
    m_sourceLabel->setMinimumSize(400, 520);
    m_sourceLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_sourceLabel->setStyleSheet(QStringLiteral("background:#eceae5; border-radius:7px; color:#7a827e;"));
    sourceLayout->addWidget(m_sourceLabel, 1);
    m_statusLabel = new QLabel(QStringLiteral("识别结果不会自动覆盖主棋盘，确认无误后再应用。"), sourcePanel);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setProperty("muted", true);
    sourceLayout->addWidget(m_statusLabel);
    m_progress = new QProgressBar(sourcePanel);
    m_progress->setRange(0, 0);
    m_progress->setMaximumHeight(4);
    m_progress->setTextVisible(false);
    m_progress->hide();
    sourceLayout->addWidget(m_progress);
    splitter->addWidget(sourcePanel);

    auto *boardPanel = new QFrame(splitter);
    boardPanel->setProperty("card", true);
    auto *boardLayout = new QVBoxLayout(boardPanel);
    boardLayout->setContentsMargins(12, 12, 12, 12);
    boardLayout->setSpacing(7);
    auto *boardHeader = new QHBoxLayout;
    auto *boardTitle = new QLabel(QStringLiteral("识别结果 · 人工校对"), boardPanel);
    boardTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto *rotateButton = new QPushButton(QStringLiteral("旋转 180°"), boardPanel);
    rotateButton->setToolTip(QStringLiteral("图片上下颠倒时，旋转识别后的棋子布局"));
    boardHeader->addWidget(boardTitle);
    boardHeader->addStretch();
    boardHeader->addWidget(rotateButton);
    boardLayout->addLayout(boardHeader);

    m_board = new BoardWidget(boardPanel);
    m_board->setObjectName(QStringLiteral("recognitionBoard"));
    m_board->setPosition(m_position);
    m_board->setEditMode(true);
    boardLayout->addWidget(m_board, 1);

    auto *correctionHint = new QLabel(QStringLiteral("左键替换 · 右键删除 · 中键拖动棋子 · 可反复修改"), boardPanel);
    correctionHint->setProperty("muted", true);
    boardLayout->addWidget(correctionHint);
    auto *palette = new QGridLayout;
    palette->setSpacing(4);
    m_paletteGroup = new QButtonGroup(this);
    m_paletteGroup->setExclusive(true);
    const QList<QPair<bool, char>> types = {
        {true, Piece::King}, {true, Piece::Advisor}, {true, Piece::Elephant}, {true, Piece::Horse},
        {true, Piece::Rook}, {true, Piece::Cannon}, {true, Piece::Pawn},
        {false, Piece::King}, {false, Piece::Advisor}, {false, Piece::Elephant}, {false, Piece::Horse},
        {false, Piece::Rook}, {false, Piece::Cannon}, {false, Piece::Pawn}
    };
    for (int i = 0; i < types.size(); ++i) {
        const char piece = Piece::make(types[i].first, types[i].second);
        auto *button = new QToolButton(boardPanel);
        const QString label = (types[i].first ? QStringLiteral("红") : QStringLiteral("黑")) + Piece::displayName(piece);
        button->setText(label);
        button->setToolTip(label);
        button->setAccessibleName(label);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIcon(QIcon(BoardWidget::piecePixmap(piece)));
        button->setIconSize(QSize(46, 46));
        button->setMinimumSize(50, 54);
        button->setCheckable(true);
        button->setProperty("piece", QString(QChar::fromLatin1(piece)));
        button->setProperty("redPiece", types[i].first);
        palette->addWidget(button, i / 7, i % 7);
        m_paletteGroup->addButton(button);
        if (piece == Piece::Rook)
            button->setChecked(true);
    }
    auto *eraser = new QToolButton(boardPanel);
    eraser->setText(QStringLiteral("橡皮"));
    eraser->setCheckable(true);
    eraser->setProperty("piece", QStringLiteral("."));
    palette->addWidget(eraser, 2, 0);
    m_paletteGroup->addButton(eraser);
    boardLayout->addLayout(palette);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(new QLabel(QStringLiteral("行棋方"), boardPanel));
    m_turnCombo = new QComboBox(boardPanel);
    m_turnCombo->addItem(QStringLiteral("红"), true);
    m_turnCombo->addItem(QStringLiteral("黑"), false);
    bottomRow->addWidget(m_turnCombo);
    bottomRow->addStretch();
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), boardPanel);
    m_applyButton = new QPushButton(QStringLiteral("校对完成，摆到主棋盘"), boardPanel);
    m_applyButton->setObjectName(QStringLiteral("primaryButton"));
    bottomRow->addWidget(cancelButton);
    bottomRow->addWidget(m_applyButton);
    boardLayout->addLayout(bottomRow);
    splitter->addWidget(boardPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({550, 570});

    connect(m_importButton, &QPushButton::clicked, this, &RecognitionDialog::importImage);
    connect(m_captureButton, &QPushButton::clicked, this, &RecognitionDialog::captureScreen);
    connect(m_recognizeButton, &QPushButton::clicked, this, [this]() {
        if (!m_sourceImage.isNull())
            loadImage(m_sourceImage, true);
    });
    connect(rotateButton, &QPushButton::clicked, this, &RecognitionDialog::rotateRecognizedBoard);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_turnCombo, &QComboBox::currentIndexChanged, this, [this](int index) { m_position.redTurn = index == 0; });
    connect(m_paletteGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *button) {
        m_palettePiece = button->property("piece").toString().at(0).toLatin1();
    });
    connect(m_board, &BoardWidget::editSquareRequested, this, [this](int square, Qt::MouseButton button) {
        if (button == Qt::RightButton || m_palettePiece == Piece::Empty) {
            m_position.setPiece(square, Piece::Empty);
        } else {
            if (Piece::typeOf(m_palettePiece) == Piece::King) {
                for (int i = 0; i < 90; ++i)
                    if (m_position.piece(i) == m_palettePiece)
                        m_position.setPiece(i, Piece::Empty);
            }
            m_position.setPiece(square, m_palettePiece);
        }
        m_board->setPosition(m_position);
        m_board->clearReviewSquare(square);
    });
    connect(m_board, &BoardWidget::editMoveRequested, this, [this](const Move &move) {
        if (!move.isValid() || m_position.piece(move.from) == Piece::Empty
            || m_position.piece(move.to) != Piece::Empty)
            return;
        const char piece = m_position.piece(move.from);
        m_position.setPiece(move.from, Piece::Empty);
        m_position.setPiece(move.to, piece);
        m_board->setPosition(m_position);
        m_board->clearReviewSquare(move.from);
        m_board->clearReviewSquare(move.to);
    });
    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        const QString error = m_position.validateForAnalysis();
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("请继续校对"), error);
            return;
        }
        emit positionAccepted(m_position);
        accept();
    });
    connect(m_recognizer, &BoardRecognizer::recognitionFinished, this,
            [this](const Position &position, const QString &details,
                   const QVector<double> &confidences,
                   const QVector<int> &colorCorrectedSquares) {
        setBusy(false);
        m_position = position;
        m_board->setPosition(m_position);
        QVector<int> reviewSquares = colorCorrectedSquares;
        for (int square = 0; square < confidences.size() && square < 90; ++square)
            if (confidences[square] < 0.65 && !reviewSquares.contains(square))
                reviewSquares.append(square);
        m_board->setReviewSquares(reviewSquares);
        {
            QSignalBlocker blocker(m_turnCombo);
            m_turnCombo->setCurrentIndex(m_position.redTurn ? 0 : 1);
        }
        setStatus(QStringLiteral("识别完成，请对照原图逐项校对后再应用。\n%1").arg(details));
    });
    connect(m_recognizer, &BoardRecognizer::recognitionFailed, this, [this](const QString &message) {
        setBusy(false);
        setStatus(message, true);
    });
}

void RecognitionDialog::loadImage(const QImage &image, bool recognize)
{
    if (image.isNull()) {
        setStatus(QStringLiteral("图片为空或无法完整解码，请重新导出为 PNG 后再试。"), true);
        return;
    }
    m_sourceImage = image.format() == QImage::Format_RGB888 ? image : image.convertToFormat(QImage::Format_RGB888);
    if (m_sourceImage.isNull()) {
        setStatus(QStringLiteral("图片转换失败，请重新导出为标准 RGB PNG。"), true);
        return;
    }
    updateSourcePreview();
    m_recognizeButton->setEnabled(true);
    if (recognize) {
        m_position = Position::empty();
        m_board->setPosition(m_position);
        m_board->setReviewSquares({});
        setBusy(true);
        setStatus(QStringLiteral("正在本地识别棋盘与棋子，请稍候…"));
        m_recognizer->recognize(m_sourceImage);
    }
}

void RecognitionDialog::importImage()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入象棋棋盘图片"), QString(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff);;所有文件 (*)"));
    if (!path.isEmpty())
        loadImageFile(path);
}

bool RecognitionDialog::loadImageFile(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage decoded = reader.read();
    if (decoded.isNull()) {
        QMessageBox::warning(this, QStringLiteral("图片读取失败"),
            QStringLiteral("无法完整解码图片：%1\n请重新导出为标准 RGB PNG。").arg(reader.errorString()));
        return false;
    }
    loadImage(decoded, true);
    return true;
}

void RecognitionDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_recognizer->isBusy()) {
        event->ignore();
        return;
    }
    const QMimeData *mime = event->mimeData();
    const auto urls = mime->urls();
    const bool hasLocalFile = std::any_of(urls.begin(), urls.end(), [](const QUrl &url) {
        return url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile();
    });
    if (hasLocalFile || mime->hasImage())
        event->acceptProposedAction();
    else
        event->ignore();
}

void RecognitionDialog::dropEvent(QDropEvent *event)
{
    if (m_recognizer->isBusy()) {
        event->ignore();
        return;
    }
    const QMimeData *mime = event->mimeData();
    const auto urls = mime->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile())
            continue;
        if (loadImageFile(url.toLocalFile()))
            event->acceptProposedAction();
        else
            event->ignore();
        return;
    }
    if (mime->hasImage()) {
        const QImage image = mime->imageData().value<QImage>();
        if (!image.isNull()) {
            loadImage(image, true);
            event->acceptProposedAction();
            return;
        }
    }
    setStatus(QStringLiteral("没有读取到可用图片，请拖入本地图片文件。"), true);
    event->ignore();
}

void RecognitionDialog::captureScreen()
{
#ifdef Q_OS_MACOS
    if (m_captureProcess)
        return;
    // screencapture exits successfully but returns only the desktop when the
    // responsible application lacks Screen Recording access. Check first so a
    // permission problem is never mistaken for an empty board photograph.
    if (!CGPreflightScreenCaptureAccess()) {
        CGRequestScreenCaptureAccess();
        if (!CGPreflightScreenCaptureAccess()) {
            setStatus(QStringLiteral(
                "请先允许棋析录制屏幕，再完全退出并重新打开应用。位置：系统设置 → 隐私与安全性 → 屏幕录制与系统录音。"),
                true);
            QMessageBox::information(this, QStringLiteral("需要屏幕录制权限"),
                QStringLiteral("macOS 尚未允许棋析读取其他窗口的画面，继续截图将只能得到桌面。\n\n"
                               "请在“系统设置 → 隐私与安全性 → 屏幕录制与系统录音”中允许“棋析”，"
                               "然后完全退出并重新打开棋析。"));
            return;
        }
    }
    if (m_recognizer->isBusy())
        m_recognizer->cancel();
    setBusy(false);

    // Hide only this application's main and recognition windows. The dialog
    // lives in the main event loop, so hiding it no longer destroys the object.
    auto *process = new QProcess(this);
    m_captureProcess = process;
    m_captureButton->setEnabled(false);
    m_captureMainWindow = parentWidget();
    if (m_captureMainWindow)
        m_captureMainWindow->hide();
    hide();
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        if (m_captureProcess == process)
            m_captureProcess = nullptr;
        process->deleteLater();
        if (m_captureMainWindow) {
            m_captureMainWindow->show();
            m_captureMainWindow->raise();
            m_captureMainWindow = nullptr;
        }
        m_captureButton->setEnabled(true);
        show();
        raise();
        activateWindow();
        if (exitCode != 0 || status != QProcess::NormalExit) {
            setStatus(QStringLiteral("已取消截图。若无法截图，请检查“系统设置 → 隐私与安全性 → 屏幕录制与系统录音”权限。"), true);
            return;
        }
        const QImage image = QGuiApplication::clipboard()->image();
        if (image.isNull()) {
            setStatus(QStringLiteral("剪贴板中没有截图，请重试或改用“导入图片”。"), true);
            return;
        }
        loadImage(image, true);
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        // Other errors are followed by finished(), which restores the window.
        if (error != QProcess::FailedToStart)
            return;
        if (m_captureProcess == process)
            m_captureProcess = nullptr;
        process->deleteLater();
        if (m_captureMainWindow) {
            m_captureMainWindow->show();
            m_captureMainWindow->raise();
            m_captureMainWindow = nullptr;
        }
        m_captureButton->setEnabled(true);
        show();
        raise();
        activateWindow();
        setStatus(QStringLiteral("无法启动 macOS 系统截图，请改用“导入图片”。"), true);
    });
    QTimer::singleShot(200, process, [process]() {
        process->start(QStringLiteral("/usr/sbin/screencapture"),
                       {QStringLiteral("-i"), QStringLiteral("-x"), QStringLiteral("-c")});
    });
#else
    setStatus(QStringLiteral("当前平台请使用“导入图片”。"), true);
#endif
}

void RecognitionDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateSourcePreview();
}

void RecognitionDialog::updateSourcePreview()
{
    if (!m_sourceLabel || m_sourceImage.isNull())
        return;
    const QSize available = m_sourceLabel->size().expandedTo({1, 1});
    m_sourceLabel->setPixmap(QPixmap::fromImage(m_sourceImage).scaled(
        available, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void RecognitionDialog::setBusy(bool busy)
{
    m_progress->setVisible(busy);
    m_captureButton->setEnabled(!busy);
    m_importButton->setEnabled(!busy);
    m_recognizeButton->setEnabled(!busy && !m_sourceImage.isNull());
    m_applyButton->setEnabled(!busy);
}

void RecognitionDialog::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#a93631;") : QString());
}

void RecognitionDialog::rotateRecognizedBoard()
{
    Position rotated;
    rotated.redTurn = m_position.redTurn;
    rotated.halfmoveClock = m_position.halfmoveClock;
    rotated.fullmoveNumber = m_position.fullmoveNumber;
    for (int row = 0; row < 10; ++row)
        for (int col = 0; col < 9; ++col)
            rotated.setPiece(Position::square(9 - row, 8 - col), m_position.piece(row, col));
    m_position = rotated;
    QVector<int> rotatedReview;
    for (int square : m_board->reviewSquares())
        rotatedReview.append(Position::square(9 - Position::rowOf(square), 8 - Position::colOf(square)));
    m_board->setPosition(m_position);
    m_board->setReviewSquares(rotatedReview);
}
