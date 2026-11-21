#include "mainwindow.h"

#include "boardwidget.h"
#include "evaluationbar.h"
#include "recognitiondialog.h"
#include "tiantiancontroller.h"
#include "variationpreviewdialog.h"
#include "windowpickerdialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QIcon textIcon(const QString &text, const QColor &color = QColor(QStringLiteral("#4e5955")))
{
    QPixmap pixmap(36, 36);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(color);
    QFont font = painter.font();
    font.setPixelSize(23);
    font.setBold(false);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
    return QIcon(pixmap);
}

QToolButton *actionButton(QAction *action, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    return button;
}

QFrame *card(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setProperty("card", true);
    return frame;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_position(Position::initial())
    , m_engine(new PikafishEngine(this))
    , m_tianTian(new TianTianController(this))
{
    QSettings settings;
    m_engine->setEnginePath(settings.value(QStringLiteral("engine/path"),
        PikafishEngine::defaultEnginePath()).toString());
    m_engine->setMoveTime(settings.value(QStringLiteral("engine/moveTime"), 3000).toInt());
    m_engine->setMultiPv(settings.value(QStringLiteral("engine/multiPv"), 3).toInt());
    m_engine->setThreads(settings.value(QStringLiteral("engine/threads"), qBound(2, QThread::idealThreadCount() / 2, 8)).toInt());
    m_engine->setHash(settings.value(QStringLiteral("engine/hash"), 128).toInt());

    buildUi();
    buildMenus();
    applyStyle();
    resetHistory();
    m_board->setPosition(m_position);
    updatePositionUi();
    resetAnalysisDisplay();

    connect(m_engine, &PikafishEngine::statusChanged, this, [this](const QString &status, bool thinking) {
        m_engineStatusLabel->setText(status);
        m_engineStatusLabel->setToolTip(QStringLiteral("本地离线分析"));
        m_progress->setVisible(thinking);
        updateActions();
    });
    connect(m_engine, &PikafishEngine::analysisUpdated, this, &MainWindow::onAnalysisUpdated);
    connect(m_engine, &PikafishEngine::analysisFinished, this, &MainWindow::onAnalysisFinished);
    connect(m_engine, &PikafishEngine::errorOccurred, this, &MainWindow::showEngineError);

    connect(m_tianTian, &TianTianController::runningChanged, this, [this](bool running) {
        m_tianTianButton->setText(running ? QStringLiteral("停止对弈") : QStringLiteral("自动对弈"));
        m_tianTianButton->setIcon(textIcon(running ? QStringLiteral("■") : QStringLiteral("▶"),
            running ? QColor(QStringLiteral("#b4433b")) : QColor(QStringLiteral("#278b78"))));
        m_tianTianButton->setProperty("running", running);
        m_tianTianButton->style()->unpolish(m_tianTianButton);
        m_tianTianButton->style()->polish(m_tianTianButton);
    });
    connect(m_tianTian, &TianTianController::statusChanged, this, [this](const QString &status) {
        m_tianTianButton->setToolTip(status);
        statusBar()->showMessage(status, 5000);
    });
    connect(m_tianTian, &TianTianController::positionChanged, this, [this](const Position &position) {
        m_position = position;
        m_updatingUi = true;
        m_turnCombo->setCurrentIndex(position.redTurn ? 0 : 1);
        m_updatingUi = false;
        resetHistory();
        m_board->setPosition(m_position);
        resetAnalysisDisplay();
        updatePositionUi();
    });
    connect(m_tianTian, &TianTianController::errorOccurred, this, [this](const QString &message) {
        auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("自动对弈已停止"), message,
                                    QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    });
    connect(m_tianTian, &TianTianController::gameFinished, this, [this](const QString &result) {
        auto *box = new QMessageBox(QMessageBox::Information, QStringLiteral("对局结束"), result,
                                    QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("棋析 · 中国象棋局势分析"));
    setWindowIcon(textIcon(QStringLiteral("棋"), QColor(QStringLiteral("#a9342f"))));
    resize(1460, 960);
    setMinimumSize(1120, 740);

    auto *toolbar = new QToolBar(QStringLiteral("主工具栏"), this);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(22, 22));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addToolBar(toolbar);

    auto *newAction = new QAction(textIcon(QStringLiteral("＋")), QStringLiteral("新局"), this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newGame);
    toolbar->addAction(newAction);

    m_editAction = new QAction(textIcon(QStringLiteral("▦")), QStringLiteral("自由摆棋"), this);
    m_editAction->setCheckable(true);
    m_editAction->setShortcut(QStringLiteral("E"));
    connect(m_editAction, &QAction::triggered, this, &MainWindow::toggleEditMode);
    toolbar->addAction(m_editAction);
    auto *recognitionAction = new QAction(textIcon(QStringLiteral("▣")), QStringLiteral("识别棋盘"), this);
    recognitionAction->setShortcut(QStringLiteral("I"));
    recognitionAction->setToolTip(QStringLiteral("从系统截图或图片识别棋盘，人工校对后摆到主棋盘"));
    connect(recognitionAction, &QAction::triggered, this, &MainWindow::recognizeBoard);
    toolbar->addAction(recognitionAction);
    toolbar->addSeparator();

    auto *turnWidget = new QWidget(toolbar);
    auto *turnLayout = new QHBoxLayout(turnWidget);
    turnLayout->setContentsMargins(4, 0, 4, 0);
    turnLayout->setSpacing(7);
    auto *turnLabel = new QLabel(QStringLiteral("行棋方"), turnWidget);
    turnLabel->setProperty("muted", true);
    m_turnCombo = new QComboBox(turnWidget);
    m_turnCombo->addItem(QStringLiteral("红"), true);
    m_turnCombo->addItem(QStringLiteral("黑"), false);
    m_turnCombo->setMinimumWidth(82);
    turnLayout->addWidget(turnLabel);
    turnLayout->addWidget(m_turnCombo);
    toolbar->addWidget(turnWidget);
    connect(m_turnCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_updatingUi)
            return;
        m_engine->stop();
        m_position.redTurn = index == 0;
        resetHistory();
        m_board->setPosition(m_position);
        resetAnalysisDisplay();
        updatePositionUi();
    });
    toolbar->addSeparator();

    m_analyzeButton = new QPushButton(QStringLiteral("开始分析"), toolbar);
    m_analyzeButton->setIcon(textIcon(QStringLiteral("⌕"), Qt::white));
    m_analyzeButton->setObjectName(QStringLiteral("primaryButton"));
    m_analyzeButton->setShortcut(QStringLiteral("Space"));
    connect(m_analyzeButton, &QPushButton::clicked, this, &MainWindow::startAnalysis);
    toolbar->addWidget(m_analyzeButton);

    m_stopButton = new QPushButton(QStringLiteral("停止"), toolbar);
    m_stopButton->setIcon(textIcon(QStringLiteral("■")));
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopAnalysis);
    toolbar->addWidget(m_stopButton);

    m_engineMoveButton = new QPushButton(QStringLiteral("电脑走棋"), toolbar);
    m_engineMoveButton->setIcon(textIcon(QStringLiteral("▶")));
    m_engineMoveButton->setToolTip(QStringLiteral("已有分析时走出第一推荐；未分析时先计算最佳着法"));
    connect(m_engineMoveButton, &QPushButton::clicked, this, &MainWindow::requestEngineMove);
    toolbar->addWidget(m_engineMoveButton);
    toolbar->addSeparator();

    m_autoAction = new QAction(QStringLiteral("连续分析"), this);
    m_autoAction->setCheckable(true);
    m_autoAction->setToolTip(QStringLiteral("每次走棋后自动分析下一局面"));
    toolbar->addAction(m_autoAction);
    toolbar->addSeparator();

    m_undoAction = new QAction(textIcon(QStringLiteral("↶")), QStringLiteral("悔棋"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);
    toolbar->addAction(m_undoAction);
    m_redoAction = new QAction(textIcon(QStringLiteral("↷")), QStringLiteral("重做"), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);
    toolbar->addAction(m_redoAction);

    m_flipAction = new QAction(textIcon(QStringLiteral("⇅")), QStringLiteral("翻转"), this);
    m_flipAction->setShortcut(QStringLiteral("F"));
    connect(m_flipAction, &QAction::triggered, this, [this]() { m_board->setFlipped(!m_board->flipped()); });
    toolbar->addAction(m_flipAction);
    auto *importAction = new QAction(textIcon(QStringLiteral("⇩")), QStringLiteral("导入 FEN"), this);
    importAction->setShortcut(QKeySequence::Open);
    connect(importAction, &QAction::triggered, this, &MainWindow::importFen);
    toolbar->addAction(importAction);
    auto *settingsAction = new QAction(textIcon(QStringLiteral("⚙")), QStringLiteral("分析设置"), this);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    toolbar->addAction(settingsAction);
    toolbar->addSeparator();
    m_tianTianButton = new QPushButton(QStringLiteral("自动对弈"), toolbar);
    m_tianTianButton->setObjectName(QStringLiteral("autoplayButton"));
    m_tianTianButton->setIcon(textIcon(QStringLiteral("▶"), QColor(QStringLiteral("#278b78"))));
    m_tianTianButton->setToolTip(QStringLiteral("选择象棋游戏窗口，自动识别、分析并点击走棋"));
    connect(m_tianTianButton, &QPushButton::clicked, this, &MainWindow::toggleTianTian);
    toolbar->addWidget(m_tianTianButton);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    setCentralWidget(splitter);

    auto *leftPanel = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(22, 16, 16, 14);
    leftLayout->setSpacing(10);
    auto *positionRow = new QHBoxLayout;
    m_positionLabel = new QLabel(leftPanel);
    m_positionLabel->setObjectName(QStringLiteral("positionTitle"));
    m_modeLabel = new QLabel(leftPanel);
    m_modeLabel->setObjectName(QStringLiteral("modeBadge"));
    positionRow->addWidget(m_positionLabel);
    positionRow->addStretch();
    positionRow->addWidget(m_modeLabel);
    leftLayout->addLayout(positionRow);

    m_board = new BoardWidget(leftPanel);
    leftLayout->addWidget(m_board, 1);
    connect(m_board, &BoardWidget::moveRequested, this, &MainWindow::onMoveRequested);
    connect(m_board, &BoardWidget::editSquareRequested, this, &MainWindow::editSquare);
    connect(m_board, &BoardWidget::editMoveRequested, this, [this](const Move &move) {
        if (!move.isValid() || m_position.piece(move.from) == Piece::Empty
            || m_position.piece(move.to) != Piece::Empty)
            return;
        const char piece = m_position.piece(move.from);
        m_position.setPiece(move.from, Piece::Empty);
        m_position.setPiece(move.to, piece);
        resetHistory();
        m_board->setPosition(m_position);
        updatePositionUi();
    });
    connect(m_board, &BoardWidget::selectionChanged, this, [this](const QString &text) {
        if (!text.isEmpty())
            statusBar()->showMessage(text, 2500);
    });

    auto *fenRow = new QHBoxLayout;
    fenRow->addWidget(actionButton(m_undoAction, leftPanel));
    fenRow->addWidget(actionButton(m_redoAction, leftPanel));
    fenRow->addSpacing(12);
    auto *fenLabel = new QLabel(QStringLiteral("FEN"), leftPanel);
    fenLabel->setProperty("muted", true);
    fenRow->addWidget(fenLabel);
    m_fenEdit = new QLineEdit(leftPanel);
    m_fenEdit->setReadOnly(true);
    m_fenEdit->setCursorPosition(0);
    m_fenEdit->setToolTip(QStringLiteral("当前局面的 FEN，可用于在其他象棋软件中恢复局面"));
    fenRow->addWidget(m_fenEdit, 1);
    auto *copyFenButton = new QPushButton(QStringLiteral("复制"), leftPanel);
    copyFenButton->setFlat(true);
    connect(copyFenButton, &QPushButton::clicked, this, &MainWindow::copyFen);
    fenRow->addWidget(copyFenButton);
    leftLayout->addLayout(fenRow);

    m_editPanel = card(leftPanel);
    auto *editLayout = new QVBoxLayout(m_editPanel);
    editLayout->setContentsMargins(12, 10, 12, 12);
    editLayout->setSpacing(8);
    auto *editTop = new QHBoxLayout;
    auto *editTitle = new QLabel(QStringLiteral("左键摆放 · 右键删除 · 中键拖动棋子"), m_editPanel);
    editTitle->setProperty("muted", true);
    editTop->addWidget(editTitle);
    editTop->addStretch();
    auto *initialButton = new QPushButton(QStringLiteral("初始布局"), m_editPanel);
    connect(initialButton, &QPushButton::clicked, this, [this]() {
        m_position = Position::initial();
        m_updatingUi = true;
        m_turnCombo->setCurrentIndex(0);
        m_updatingUi = false;
        resetHistory();
        m_board->setPosition(m_position);
        updatePositionUi();
    });
    editTop->addWidget(initialButton);
    auto *clearButton = new QPushButton(QStringLiteral("清空"), m_editPanel);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearBoard);
    editTop->addWidget(clearButton);
    auto *doneButton = new QPushButton(QStringLiteral("完成摆棋"), m_editPanel);
    doneButton->setObjectName(QStringLiteral("secondaryAccent"));
    connect(doneButton, &QPushButton::clicked, this, &MainWindow::toggleEditMode);
    editTop->addWidget(doneButton);
    editLayout->addLayout(editTop);

    auto *palette = new QGridLayout;
    palette->setSpacing(5);
    m_paletteGroup = new QButtonGroup(this);
    m_paletteGroup->setExclusive(true);
    const QList<QPair<bool, char>> types = {
        {true, Piece::King}, {true, Piece::Advisor}, {true, Piece::Elephant}, {true, Piece::Horse},
        {true, Piece::Rook}, {true, Piece::Cannon}, {true, Piece::Pawn},
        {false, Piece::King}, {false, Piece::Advisor}, {false, Piece::Elephant}, {false, Piece::Horse},
        {false, Piece::Rook}, {false, Piece::Cannon}, {false, Piece::Pawn}
    };
    for (int i = 0; i < types.size(); ++i) {
        const char p = Piece::make(types[i].first, types[i].second);
        auto *button = new QToolButton(m_editPanel);
        const QString label = (types[i].first ? QStringLiteral("红") : QStringLiteral("黑")) + Piece::displayName(p);
        button->setText(label);
        button->setToolTip(label);
        button->setAccessibleName(label);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIcon(QIcon(BoardWidget::piecePixmap(p)));
        button->setIconSize(QSize(46, 46));
        button->setCheckable(true);
        button->setProperty("piece", QString(QChar::fromLatin1(p)));
        button->setMinimumSize(50, 54);
        button->setProperty("redPiece", types[i].first);
        palette->addWidget(button, i / 7, i % 7);
        m_paletteGroup->addButton(button);
        if (p == Piece::Rook)
            button->setChecked(true);
    }
    auto *eraser = new QToolButton(m_editPanel);
    eraser->setText(QStringLiteral("橡皮"));
    eraser->setCheckable(true);
    eraser->setProperty("piece", QStringLiteral("."));
    palette->addWidget(eraser, 1, 7);
    m_paletteGroup->addButton(eraser);
    connect(m_paletteGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *button) {
        m_palettePiece = button->property("piece").toString().at(0).toLatin1();
    });
    editLayout->addLayout(palette);
    m_editPanel->hide();
    leftLayout->addWidget(m_editPanel);

    auto *rightPanel = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(4, 12, 16, 14);
    m_sideTabs = new QTabWidget(rightPanel);
    rightLayout->addWidget(m_sideTabs);

    auto *analysisTab = new QWidget(m_sideTabs);
    auto *analysisLayout = new QVBoxLayout(analysisTab);
    analysisLayout->setContentsMargins(0, 10, 0, 0);
    analysisLayout->setSpacing(9);
    auto *evaluationCard = card(analysisTab);
    evaluationCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *evaluationLayout = new QVBoxLayout(evaluationCard);
    evaluationLayout->setContentsMargins(16, 10, 16, 10);
    evaluationLayout->setSpacing(5);
    auto *evaluationHeader = new QHBoxLayout;
    auto *evaluationTitle = new QLabel(QStringLiteral("局面评估"), evaluationCard);
    evaluationTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_depthLabel = new QLabel(QStringLiteral("未分析"), evaluationCard);
    m_depthLabel->setProperty("muted", true);
    evaluationHeader->addWidget(evaluationTitle);
    evaluationHeader->addStretch();
    evaluationHeader->addWidget(m_depthLabel);
    evaluationLayout->addLayout(evaluationHeader);
    m_scoreLabel = new QLabel(QStringLiteral("—"), evaluationCard);
    m_scoreLabel->setObjectName(QStringLiteral("scoreLabel"));
    m_assessmentLabel = new QLabel(QStringLiteral("等待分析"), evaluationCard);
    m_assessmentLabel->setProperty("muted", true);
    auto *scoreRow = new QHBoxLayout;
    scoreRow->setSpacing(12);
    scoreRow->addWidget(m_scoreLabel);
    scoreRow->addWidget(m_assessmentLabel, 0, Qt::AlignBottom);
    scoreRow->addStretch();
    evaluationLayout->addLayout(scoreRow);
    m_evaluationBar = new EvaluationBar(evaluationCard);
    evaluationLayout->addWidget(m_evaluationBar);
    auto *scoreSides = new QHBoxLayout;
    auto *redSide = new QLabel(QStringLiteral("红方优势"), evaluationCard);
    redSide->setObjectName(QStringLiteral("redText"));
    auto *blackSide = new QLabel(QStringLiteral("黑方优势"), evaluationCard);
    blackSide->setProperty("muted", true);
    scoreSides->addWidget(redSide);
    scoreSides->addStretch();
    scoreSides->addWidget(blackSide);
    evaluationLayout->addLayout(scoreSides);
    m_progress = new QProgressBar(evaluationCard);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(3);
    m_progress->hide();
    evaluationLayout->addWidget(m_progress);
    analysisLayout->addWidget(evaluationCard);

    auto *candidateHeader = new QHBoxLayout;
    auto *candidateTitle = new QLabel(QStringLiteral("推荐着法"), analysisTab);
    candidateTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto *candidateHint = new QLabel(QStringLiteral("单击看箭头 · 双击走棋"), analysisTab);
    candidateHint->setProperty("muted", true);
    candidateHeader->addWidget(candidateTitle);
    candidateHeader->addStretch();
    candidateHeader->addWidget(candidateHint);
    analysisLayout->addLayout(candidateHeader);

    m_candidateTable = new QTableWidget(0, 4, analysisTab);
    m_candidateTable->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("着法"), QStringLiteral("红方评分"), QStringLiteral("深度")});
    m_candidateTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_candidateTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_candidateTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_candidateTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_candidateTable->verticalHeader()->setVisible(false);
    m_candidateTable->setShowGrid(false);
    m_candidateTable->setAlternatingRowColors(true);
    m_candidateTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_candidateTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_candidateTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_candidateTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_candidateTable->setFixedHeight(146);
    m_candidateTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(m_candidateTable, &QTableWidget::cellClicked, this, [this](int row, int) { showSelectedLine(row); });
    connect(m_candidateTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::playSelectedLine);
    analysisLayout->addWidget(m_candidateTable);

    auto *variationCard = card(analysisTab);
    auto *variationLayout = new QVBoxLayout(variationCard);
    variationLayout->setContentsMargins(14, 10, 14, 10);
    auto *variationTitle = new QLabel(QStringLiteral("主要变化 · 点击着法预览局面"), variationCard);
    variationTitle->setObjectName(QStringLiteral("sectionTitle"));
    variationLayout->addWidget(variationTitle);
    m_variationText = new QTextBrowser(variationCard);
    m_variationText->setOpenLinks(false);
    m_variationText->setOpenExternalLinks(false);
    m_variationText->setFrameShape(QFrame::NoFrame);
    m_variationText->setMinimumHeight(220);
    m_variationText->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    connect(m_variationText, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() != QStringLiteral("pv"))
            return;
        previewVariation(url.host().toInt(), url.path().mid(1).toInt());
    });
    variationLayout->addWidget(m_variationText);
    analysisLayout->addWidget(variationCard, 1);
    m_selectedMoveButton = new QPushButton(QStringLiteral("走出选中推荐"), analysisTab);
    m_selectedMoveButton->setObjectName(QStringLiteral("secondaryAccent"));
    connect(m_selectedMoveButton, &QPushButton::clicked, this, [this]() {
        if (m_candidateTable->currentRow() >= 0)
            playSelectedLine(m_candidateTable->currentRow(), 0);
    });
    analysisLayout->addWidget(m_selectedMoveButton);
    m_sideTabs->addTab(analysisTab, QStringLiteral("局势分析"));

    auto *movesTab = new QWidget(m_sideTabs);
    auto *movesLayout = new QVBoxLayout(movesTab);
    movesLayout->setContentsMargins(0, 12, 0, 0);
    m_moveTable = new QTableWidget(0, 3, movesTab);
    m_moveTable->setHorizontalHeaderLabels({QStringLiteral("回合"), QStringLiteral("红方"), QStringLiteral("黑方")});
    m_moveTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_moveTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_moveTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_moveTable->verticalHeader()->setVisible(false);
    m_moveTable->setShowGrid(false);
    m_moveTable->setAlternatingRowColors(true);
    m_moveTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_moveTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_moveTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    connect(m_moveTable, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column > 0)
            navigateToPly(row * 2 + column);
    });
    movesLayout->addWidget(m_moveTable);
    auto *copyMovesButton = new QPushButton(QStringLiteral("复制棋谱"), movesTab);
    connect(copyMovesButton, &QPushButton::clicked, this, &MainWindow::copyMoves);
    movesLayout->addWidget(copyMovesButton);
    m_sideTabs->addTab(movesTab, QStringLiteral("棋谱"));

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({850, 510});

    m_engineStatusLabel = new QLabel(QStringLiteral("分析待启动"), this);
    m_engineStatusLabel->setToolTip(QStringLiteral("本地离线分析"));
    statusBar()->addPermanentWidget(m_engineStatusLabel);
    statusBar()->showMessage(QStringLiteral("单击棋子走棋，或点击“自由摆棋”设置局面"), 6000);
}

void MainWindow::buildMenus()
{
    auto *gameMenu = menuBar()->addMenu(QStringLiteral("对局"));
    gameMenu->addAction(QStringLiteral("新对局"), this, &MainWindow::newGame, QKeySequence::New);
    gameMenu->addAction(QStringLiteral("导入 FEN…"), this, &MainWindow::importFen, QKeySequence::Open);
    gameMenu->addAction(QStringLiteral("截图／图片识别棋盘…"), this, &MainWindow::recognizeBoard, QStringLiteral("I"));
    gameMenu->addSeparator();
    gameMenu->addAction(m_undoAction);
    gameMenu->addAction(m_redoAction);
    gameMenu->addAction(m_flipAction);
    gameMenu->addSeparator();
    gameMenu->addAction(QStringLiteral("退出"), qApp, &QApplication::quit, QKeySequence::Quit);

    auto *analysisMenu = menuBar()->addMenu(QStringLiteral("分析"));
    analysisMenu->addAction(QStringLiteral("开始分析"), this, &MainWindow::startAnalysis, QStringLiteral("Space"));
    analysisMenu->addAction(QStringLiteral("停止分析"), this, &MainWindow::stopAnalysis, QStringLiteral("Esc"));
    analysisMenu->addAction(QStringLiteral("电脑走棋"), this, &MainWindow::requestEngineMove, QStringLiteral("Return"));
    analysisMenu->addAction(m_autoAction);
    analysisMenu->addSeparator();
    analysisMenu->addAction(QStringLiteral("分析设置…"), this, &MainWindow::showSettings);

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    helpMenu->addAction(QStringLiteral("关于棋析"), this, &MainWindow::showAbout);
}

void MainWindow::applyStyle()
{
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QDialog { background: #f4f2ee; }
        QWidget { color: #28332f; font-size: 13px; }
        QToolBar { background: #fbfaf8; border: 0; border-bottom: 1px solid #e4e1da; padding: 7px 10px; spacing: 3px; }
        QToolBar QToolButton, QToolBar QPushButton { padding: 7px 9px; border-radius: 6px; background: transparent; }
        QToolBar QToolButton:hover, QToolBar QPushButton:hover { background: #eeece6; }
        QToolBar QToolButton:checked { background: #e1eeea; color: #197766; }
        QPushButton, QToolButton { padding: 7px 12px; border: 1px solid #dcded8; border-radius: 6px; background: #fff; }
        QPushButton:hover, QToolButton:hover { background: #f0f4f1; border-color: #b8c8c0; }
        QPushButton:disabled, QToolButton:disabled { color: #a8ada9; background: #f2f2ef; border-color: #e5e5e0; }
        QPushButton#primaryButton { background: #237f6d; color: white; border: 1px solid #237f6d; font-weight: 600; padding: 8px 15px; }
        QPushButton#primaryButton:hover { background: #196e5e; }
        QPushButton#primaryButton:disabled { background: #9bb8af; border-color: #9bb8af; }
        QPushButton#secondaryAccent { color: #19715f; background: #e8f3ef; border-color: #bcd9ce; font-weight: 600; }
        QComboBox, QLineEdit, QSpinBox { background: #fff; border: 1px solid #dcded8; border-radius: 5px; padding: 5px 8px; }
        QComboBox QAbstractItemView { background: white; selection-background-color: #dcece6; }
        QLabel[muted="true"] { color: #7a827e; font-size: 12px; }
        QLabel#positionTitle { font-size: 16px; font-weight: 600; }
        QLabel#modeBadge { color: #5c6f67; background: #e8ece8; border-radius: 5px; padding: 4px 9px; font-size: 12px; }
        QLabel#sectionTitle { font-size: 15px; font-weight: 600; }
        QLabel#scoreLabel { font-size: 32px; font-weight: 700; color: #a93631; }
        QLabel#redText { color: #b13c36; font-size: 11px; }
        QFrame[card="true"] { background: #fff; border: 1px solid #e4e3de; border-radius: 10px; }
        QTabWidget::pane { border: 0; }
        QTabBar::tab { background: transparent; padding: 10px 17px; color: #747d78; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { color: #1d7563; border-bottom-color: #278875; font-weight: 600; }
        QTableWidget { background: #fff; alternate-background-color: #f8f9f6; border: 1px solid #e4e5df; border-radius: 8px; selection-background-color: #dfeee8; selection-color: #174f43; }
        QTableWidget::item { padding: 7px 5px; border: 0; }
        QHeaderView::section { background: #f7f7f3; color: #737b77; border: 0; border-bottom: 1px solid #e4e5df; padding: 7px; font-size: 12px; }
        QTextBrowser { background: transparent; color: #4b5651; line-height: 1.5; }
        QLineEdit[readOnly="true"] { background: transparent; border: 0; color: #8a8f8c; font-size: 11px; }
        QProgressBar { border: 0; background: #e3e9e5; }
        QProgressBar::chunk { background: #278875; }
        QStatusBar { background: #fbfaf8; border-top: 1px solid #e4e1da; color: #747d78; }
        QSplitter::handle { background: #e2e0da; }
        QMenuBar { background: #fbfaf8; }
        QMenu { background: #fff; border: 1px solid #ddd; }
        QMenu::item:selected { background: #dfeee8; }
    )"));
}

void MainWindow::newGame()
{
    if (m_historyIndex > 0 && !m_editAction->isChecked()) {
        const auto answer = QMessageBox::question(this, QStringLiteral("开始新对局"),
            QStringLiteral("当前棋谱尚未保存，确定开始新对局吗？"));
        if (answer != QMessageBox::Yes)
            return;
    }
    m_engine->stop();
    if (m_editAction->isChecked()) {
        m_editAction->setChecked(false);
        m_board->setEditMode(false);
        m_editPanel->hide();
    }
    m_position = Position::initial();
    m_updatingUi = true;
    m_turnCombo->setCurrentIndex(0);
    m_updatingUi = false;
    resetHistory();
    m_board->setPosition(m_position);
    m_board->setLastMove({});
    resetAnalysisDisplay();
    updatePositionUi();
}

void MainWindow::toggleEditMode()
{
    const bool enter = !m_board->editMode();
    if (!enter) {
        const QString error = m_position.validateForAnalysis();
        if (!error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("还不能完成摆棋"), error);
            m_editAction->setChecked(true);
            return;
        }
    } else {
        m_engine->stop();
    }
    m_board->setEditMode(enter);
    m_editAction->setChecked(enter);
    m_editPanel->setVisible(enter);
    resetAnalysisDisplay();
    resetHistory();
    updatePositionUi();
}

void MainWindow::editSquare(int square, Qt::MouseButton button)
{
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
    resetHistory();
    m_board->setPosition(m_position);
    updatePositionUi();
}

void MainWindow::clearBoard()
{
    m_position.clear();
    m_position.redTurn = m_turnCombo->currentIndex() == 0;
    resetHistory();
    m_board->setPosition(m_position);
    resetAnalysisDisplay();
    updatePositionUi();
}

void MainWindow::startAnalysis()
{
    if (m_board->editMode())
        toggleEditMode();
    if (m_board->editMode())
        return;
    const QString error = m_position.validateForAnalysis();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法分析当前局面"), error);
        return;
    }
    if (!m_position.hasAnyLegalMove()) {
        QMessageBox::information(this, QStringLiteral("对局结束"),
            m_position.redTurn ? QStringLiteral("黑方胜") : QStringLiteral("红方胜"));
        return;
    }
    m_variationText->setPlainText(QStringLiteral("正在计算候选着法…"));
    m_engine->analyze(m_position, false);
}

void MainWindow::stopAnalysis()
{
    m_engine->stop();
    m_progress->hide();
    updateActions();
}

void MainWindow::requestEngineMove()
{
    if (m_board->editMode())
        toggleEditMode();
    if (m_board->editMode())
        return;
    const QString error = m_position.validateForAnalysis();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法走棋"), error);
        return;
    }
    if (!m_lines.isEmpty() && m_position.isLegalMove(m_lines.first().firstMove)) {
        commitMove(m_lines.first().firstMove);
        return;
    }
    m_engine->analyze(m_position, true);
}

void MainWindow::onMoveRequested(const Move &move)
{
    if (!m_board->editMode() && m_position.isLegalMove(move))
        commitMove(move);
}

void MainWindow::commitMove(const Move &move)
{
    if (!m_position.isLegalMove(move))
        return;
    m_engine->stop();
    const QString notation = m_position.moveNotation(move);
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    while (m_moveNotations.size() > m_historyIndex)
        m_moveNotations.removeLast();
    m_moveNotations.append(notation);
    m_position.applyMove(move);
    m_history.append(m_position);
    ++m_historyIndex;
    m_board->setPosition(m_position);
    m_board->setLastMove(move);
    resetAnalysisDisplay();
    updatePositionUi();
    statusBar()->showMessage(QStringLiteral("走棋：%1（%2）").arg(notation, Position::moveToUci(move)), 3500);
    if (m_autoAction->isChecked())
        QTimer::singleShot(180, this, &MainWindow::startAnalysis);
}

void MainWindow::onAnalysisUpdated(const QVector<EngineLine> &lines, bool redToMove)
{
    m_lines = lines;
    m_linesRedToMove = redToMove;
    if (lines.isEmpty()) {
        m_candidateTable->setRowCount(0);
        m_candidateTable->setFixedHeight(76);
        m_selectedMoveButton->setEnabled(false);
        return;
    }
    m_candidateTable->setRowCount(lines.size());
    for (int row = 0; row < lines.size(); ++row) {
        const EngineLine &line = lines[row];
        const int redCp = redToMove ? line.scoreCp : -line.scoreCp;
        const int redMate = redToMove ? line.mate : -line.mate;
        auto *rank = new QTableWidgetItem(QString::number(row + 1));
        auto *move = new QTableWidgetItem(line.notation);
        auto *score = new QTableWidgetItem(scoreText(redCp, redMate));
        auto *depth = new QTableWidgetItem(QString::number(line.depth));
        rank->setTextAlignment(Qt::AlignCenter);
        score->setTextAlignment(Qt::AlignCenter);
        depth->setTextAlignment(Qt::AlignCenter);
        rank->setData(Qt::UserRole, line.firstMove.from);
        rank->setData(Qt::UserRole + 1, line.firstMove.to);
        if (row == 0) {
            QFont font = move->font();
            font.setBold(true);
            move->setFont(font);
        }
        score->setForeground(redCp >= 0 ? QColor(QStringLiteral("#a93631")) : QColor(QStringLiteral("#343b38")));
        m_candidateTable->setItem(row, 0, rank);
        m_candidateTable->setItem(row, 1, move);
        m_candidateTable->setItem(row, 2, score);
        m_candidateTable->setItem(row, 3, depth);
        m_candidateTable->setRowHeight(row, 36);
    }
    const int visibleRows = qBound(1, static_cast<int>(lines.size()), 4);
    m_candidateTable->setFixedHeight(qMax(30, m_candidateTable->horizontalHeader()->height())
                                     + visibleRows * 36 + 6);
    const EngineLine &best = lines.first();
    const int redCp = redToMove ? best.scoreCp : -best.scoreCp;
    const int redMate = redToMove ? best.mate : -best.mate;
    m_scoreLabel->setText(scoreText(redCp, redMate));
    m_assessmentLabel->setText(assessmentText(redCp, redMate) + QStringLiteral(" · 红方视角"));
    m_evaluationBar->setEvaluation(redCp, redMate);
    QString nodes = best.nodes >= 10000 ? QStringLiteral("%1 万").arg(best.nodes / 10000.0, 0, 'f', 1)
                                       : QString::number(best.nodes);
    m_depthLabel->setText(QStringLiteral("深度 %1 · %2 节点").arg(best.depth).arg(nodes));
    if (m_candidateTable->currentRow() < 0 || m_candidateTable->currentRow() >= lines.size())
        m_candidateTable->selectRow(0);
    showSelectedLine(m_candidateTable->currentRow());
    m_selectedMoveButton->setEnabled(!m_engine->isThinking() || best.firstMove.isValid());
    updateActions();
}

QString MainWindow::scoreText(int redCp, int redMate) const
{
    if (redMate != 0)
        return QStringLiteral("%1方 %2 步杀")
            .arg(redMate > 0 ? QStringLiteral("红") : QStringLiteral("黑"))
            .arg(std::abs(redMate));
    return redCp > 0 ? QStringLiteral("+%1").arg(redCp) : QString::number(redCp);
}

QString MainWindow::assessmentText(int redCp, int redMate) const
{
    if (redMate != 0)
        return redMate > 0 ? QStringLiteral("红方可强制将死") : QStringLiteral("黑方可强制将死");
    const int value = std::abs(redCp);
    const bool red = redCp >= 0;
    if (value == 0)
        return QStringLiteral("双方基本均势");
    if (value <= 200)
        return red ? QStringLiteral("红方稍优") : QStringLiteral("黑方稍优");
    return red ? QStringLiteral("红方大优") : QStringLiteral("黑方大优");
}

void MainWindow::showSelectedLine(int row)
{
    if (row < 0 || row >= m_lines.size()) {
        m_variationText->setPlainText(QStringLiteral("开始分析后，这里会显示推荐着法的后续变化。"));
        m_selectedMoveButton->setEnabled(false);
        return;
    }
    const EngineLine &line = m_lines[row];
    m_board->setHintMove(line.firstMove);
    m_selectedMoveButton->setEnabled(!m_board->editMode());
    updateVariations();
}

void MainWindow::updateVariations()
{
    if (m_lines.isEmpty()) {
        m_variationText->setPlainText(QStringLiteral("开始分析后，这里会显示推荐着法的后续变化。"));
        return;
    }
    const int selectedRow = m_candidateTable->currentRow();
    QString html = QStringLiteral("<html><body style='margin:0; padding:0; font-family:\"PingFang SC\";'>");
    for (int row = 0; row < m_lines.size(); ++row) {
        const EngineLine &line = m_lines[row];
        const int redCp = m_linesRedToMove ? line.scoreCp : -line.scoreCp;
        const int redMate = m_linesRedToMove ? line.mate : -line.mate;
        const bool redFavored = redMate != 0 ? redMate > 0 : redCp >= 0;
        const int displayCp = redFavored ? redCp : -redCp;
        const int displayMate = redFavored ? redMate : -redMate;
        const QString headerScore = redMate != 0
            ? QStringLiteral("#%1").arg(std::abs(redMate))
            : scoreText(displayCp, 0);
        QString nodes;
        if (line.nodes >= 100000000)
            nodes = QStringLiteral("%1亿").arg(line.nodes / 100000000.0, 0, 'f', 1);
        else if (line.nodes >= 10000)
            nodes = QStringLiteral("%1万").arg(line.nodes / 10000.0, 0, 'f', 1);
        else if (line.nodes >= 1000)
            nodes = QStringLiteral("%1K").arg(line.nodes / 1000.0, 0, 'f', 1);
        else
            nodes = QString::number(line.nodes);
        const QString background = row == selectedRow ? QStringLiteral("#eef7f4") : QStringLiteral("#f7f9fb");
        const QString border = row == selectedRow ? QStringLiteral("#78ad9f") : QStringLiteral("#e5e9ed");
        html += QStringLiteral(
            "<a name='line%1'></a>"
            "<table width='100%' cellspacing='0' cellpadding='9' "
            "style='background-color:%2; border:1px solid %3; margin-bottom:10px;'>"
            "<tr><td>"
            "<span style='background-color:#e5eefb; color:#346db5; font-size:16px; font-weight:600;'>"
            "&nbsp;%4 %5&nbsp;</span>"
            "<span style='color:#929aa2; font-size:12px;'>&nbsp;&nbsp;D%6"
            "&nbsp;&nbsp;%7s&nbsp;&nbsp;%8</span>"
            "</td></tr>"
            "<tr><td style='padding-top:5px; line-height:185%;'>%9</td></tr>"
            "</table><div style='font-size:6px;'>&nbsp;</div>")
            .arg(row + 1)
            .arg(background, border)
            .arg(redFavored ? QStringLiteral("红分") : QStringLiteral("黑分"), headerScore)
            .arg(line.depth)
            .arg(line.timeMs / 1000.0, 0, 'f', 1)
            .arg(nodes)
            .arg(pvText(line, row));
    }
    html += QStringLiteral("</body></html>");
    m_variationText->setHtml(html);
    if (selectedRow > 0)
        m_variationText->scrollToAnchor(QStringLiteral("line%1").arg(selectedRow + 1));
}

QString MainWindow::pvText(const EngineLine &line, int lineIndex) const
{
    Position position = m_position;
    QString html;
    constexpr int movesPerRow = 6;
    constexpr int maxMoves = 24;
    for (int i = 0; i < line.pv.size() && i < maxMoves; ++i) {
        const Move move = Position::moveFromUci(line.pv[i]);
        if (!move.isValid())
            break;
        if (i > 0)
            html += i % movesPerRow == 0 ? QStringLiteral("<br>") : QStringLiteral("&nbsp;&nbsp;&nbsp;");
        const QString notation = position.moveNotation(move).toHtmlEscaped();
        const QString color = position.redTurn ? QStringLiteral("#a33d3d") : QStringLiteral("#252a2d");
        html += QStringLiteral("<a href='pv://%1/%2' style='text-decoration:none; color:%3;' title='点击查看此步局面'>"
                               "<span style='font-size:15px; font-weight:%4;'>%5</span></a>")
            .arg(lineIndex)
            .arg(i)
            .arg(color, position.redTurn ? QStringLiteral("600") : QStringLiteral("400"), notation);
        position.applyMove(move);
    }
    if (line.pv.size() > maxMoves)
        html += QStringLiteral("&nbsp;<span style='color:#929aa2;'>…</span>");
    return html;
}

void MainWindow::previewVariation(int lineIndex, int ply)
{
    if (lineIndex < 0 || lineIndex >= m_lines.size() || ply < 0)
        return;
    const Position initial = m_position;
    const QString initialFen = initial.toFen();
    auto *dialog = new VariationPreviewDialog(initial, m_lines[lineIndex].pv, ply + 1, this);
    dialog->setObjectName(QStringLiteral("variationPreview"));
    connect(dialog, &VariationPreviewDialog::applyRequested, this,
            [this, initialFen](const QVector<Move> &moves) {
        if (m_position.toFen() != initialFen) {
            QMessageBox::warning(this, QStringLiteral("局面已经变化"),
                QStringLiteral("主棋盘已不再是对应的分析局面，请重新分析后再应用变化。"));
            return;
        }
        const bool continuous = m_autoAction->isChecked();
        m_autoAction->setChecked(false);
        for (const Move &move : moves)
            commitMove(move);
        m_autoAction->setChecked(continuous);
        if (continuous)
            QTimer::singleShot(180, this, &MainWindow::startAnalysis);
        statusBar()->showMessage(QStringLiteral("已将 %1 步变化走到主棋盘").arg(moves.size()), 3500);
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::onAnalysisFinished(const Move &bestMove, const QString &notation, bool playMove)
{
    m_progress->hide();
    if (bestMove.isValid()) {
        m_board->setHintMove(bestMove);
        if (playMove)
            commitMove(bestMove);
        else
            statusBar()->showMessage(QStringLiteral("最佳着法：%1 · 双击推荐可直接走棋").arg(notation), 5000);
    } else if (!notation.isEmpty()) {
        statusBar()->showMessage(notation, 4000);
    }
    updateActions();
}

void MainWindow::playSelectedLine(int row, int)
{
    if (row < 0 || row >= m_lines.size() || m_engine->isThinking())
        return;
    const Move move = m_lines[row].firstMove;
    if (m_position.isLegalMove(move))
        commitMove(move);
}

void MainWindow::undo()
{
    if (m_historyIndex <= 0)
        return;
    m_engine->stop();
    --m_historyIndex;
    m_position = m_history[m_historyIndex];
    m_board->setPosition(m_position);
    m_board->setLastMove({});
    resetAnalysisDisplay();
    updatePositionUi();
}

void MainWindow::redo()
{
    if (m_historyIndex + 1 >= m_history.size())
        return;
    m_engine->stop();
    ++m_historyIndex;
    m_position = m_history[m_historyIndex];
    m_board->setPosition(m_position);
    m_board->setLastMove({});
    resetAnalysisDisplay();
    updatePositionUi();
}

void MainWindow::navigateToPly(int ply)
{
    if (ply < 0 || ply >= m_history.size() || ply == m_historyIndex)
        return;
    m_engine->stop();
    m_historyIndex = ply;
    m_position = m_history[m_historyIndex];
    m_board->setPosition(m_position);
    m_board->setLastMove({});
    resetAnalysisDisplay();
    updatePositionUi();
}

void MainWindow::resetHistory()
{
    m_history.clear();
    m_history.append(m_position);
    m_historyIndex = 0;
    m_moveNotations.clear();
}

void MainWindow::updatePositionUi()
{
    m_updatingUi = true;
    m_turnCombo->setCurrentIndex(m_position.redTurn ? 0 : 1);
    m_updatingUi = false;
    m_fenEdit->setText(m_position.toFen());
    m_fenEdit->setCursorPosition(0);
    if (m_board->editMode()) {
        m_positionLabel->setText(QStringLiteral("自由摆棋 · %1").arg(m_position.redTurn ? QStringLiteral("红方先行") : QStringLiteral("黑方先行")));
        m_modeLabel->setText(QStringLiteral("摆棋模式"));
    } else {
        QString state = m_position.redTurn ? QStringLiteral("红方行棋") : QStringLiteral("黑方行棋");
        if (m_position.validateForAnalysis().isEmpty()) {
            if (m_position.isInCheck(m_position.redTurn))
                state += QStringLiteral(" · 被将军");
            if (!m_position.hasAnyLegalMove())
                state = m_position.redTurn ? QStringLiteral("黑方胜") : QStringLiteral("红方胜");
        }
        m_positionLabel->setText(QStringLiteral("第 %1 回合 · %2").arg(m_position.fullmoveNumber).arg(state));
        m_modeLabel->setText(QStringLiteral("对局模式"));
    }
    updateMoveTable();
    updateActions();
}

void MainWindow::updateMoveTable()
{
    const int rows = (m_moveNotations.size() + 1) / 2;
    m_moveTable->setRowCount(rows);
    for (int row = 0; row < rows; ++row) {
        auto *number = new QTableWidgetItem(QString::number(row + 1));
        number->setTextAlignment(Qt::AlignCenter);
        number->setForeground(QColor(QStringLiteral("#8a918d")));
        m_moveTable->setItem(row, 0, number);
        for (int col = 0; col < 2; ++col) {
            const int ply = row * 2 + col;
            auto *item = new QTableWidgetItem(ply < m_moveNotations.size() ? m_moveNotations[ply] : QString());
            item->setTextAlignment(Qt::AlignCenter);
            if (ply == m_historyIndex - 1) {
                QFont font = item->font();
                font.setBold(true);
                item->setFont(font);
                item->setBackground(QColor(QStringLiteral("#dfeee8")));
            }
            m_moveTable->setItem(row, col + 1, item);
        }
        m_moveTable->setRowHeight(row, 34);
    }
}

void MainWindow::updateActions()
{
    const bool thinking = m_engine->isThinking();
    const bool editing = m_board->editMode();
    m_analyzeButton->setEnabled(!thinking && !editing);
    m_analyzeButton->setText(thinking ? QStringLiteral("分析中…") : QStringLiteral("开始分析"));
    m_stopButton->setEnabled(thinking);
    m_engineMoveButton->setEnabled(!thinking && !editing);
    m_selectedMoveButton->setEnabled(!editing && !thinking && !m_lines.isEmpty());
    m_undoAction->setEnabled(m_historyIndex > 0 && !thinking);
    m_redoAction->setEnabled(m_historyIndex + 1 < m_history.size() && !thinking);
}

void MainWindow::resetAnalysisDisplay()
{
    m_lines.clear();
    m_candidateTable->setRowCount(0);
    m_candidateTable->setFixedHeight(76);
    m_scoreLabel->setText(QStringLiteral("—"));
    m_assessmentLabel->setText(QStringLiteral("等待分析"));
    m_depthLabel->setText(QStringLiteral("未分析"));
    m_evaluationBar->setEvaluation(0, 0);
    m_variationText->setPlainText(QStringLiteral("开始分析后，这里会显示推荐着法的后续变化。\n\n评分统一采用红方视角：正分红优，负分黑优。"));
    m_selectedMoveButton->setEnabled(false);
    m_board->setHintMove({});
}

void MainWindow::importFen()
{
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(this, QStringLiteral("导入 FEN"),
        QStringLiteral("粘贴包含行棋方的 FEN："), m_position.toFen(), &accepted);
    if (!accepted)
        return;
    Position imported;
    QString error;
    if (!Position::fromFen(text, imported, &error)) {
        QMessageBox::warning(this, QStringLiteral("FEN 格式错误"), error);
        return;
    }
    error = imported.validateForAnalysis();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("局面无法使用"), error);
        return;
    }
    m_engine->stop();
    if (m_board->editMode()) {
        m_board->setEditMode(false);
        m_editAction->setChecked(false);
        m_editPanel->hide();
    }
    m_position = imported;
    resetHistory();
    m_board->setPosition(m_position);
    m_board->setLastMove({});
    resetAnalysisDisplay();
    updatePositionUi();
}

void MainWindow::recognizeBoard()
{
    m_engine->stop();
    auto *dialog = new RecognitionDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &RecognitionDialog::positionAccepted, this, [this](const Position &recognized) {
        if (m_board->editMode()) {
            m_board->setEditMode(false);
            m_editAction->setChecked(false);
            m_editPanel->hide();
        }
        m_position = recognized;
        resetHistory();
        m_board->setPosition(m_position);
        m_board->setLastMove({});
        resetAnalysisDisplay();
        updatePositionUi();
        statusBar()->showMessage(QStringLiteral("已应用校对后的识别局面，请确认行棋方后开始分析"), 5000);
    });
    // Keep the dialog in the application's main event loop. A stack exec()
    // dialog returns when hidden for interactive screencapture, destroying the
    // dialog while QProcess still has completion callbacks aimed at it.
    dialog->open();
}

void MainWindow::copyFen()
{
    QGuiApplication::clipboard()->setText(m_position.toFen());
    statusBar()->showMessage(QStringLiteral("FEN 已复制"), 2000);
}

void MainWindow::copyMoves()
{
    QStringList lines;
    for (int i = 0; i < m_moveNotations.size(); i += 2) {
        QString line = QStringLiteral("%1. %2").arg(i / 2 + 1).arg(m_moveNotations[i]);
        if (i + 1 < m_moveNotations.size())
            line += QStringLiteral("  ") + m_moveNotations[i + 1];
        lines.append(line);
    }
    lines.append(QStringLiteral("\nFEN: %1").arg(m_position.toFen()));
    QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    statusBar()->showMessage(QStringLiteral("棋谱和当前 FEN 已复制"), 2000);
}

void MainWindow::showSettings()
{
    QSettings settings;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("分析设置"));
    dialog.setMinimumWidth(570);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    auto *enginePathEdit = new QLineEdit(&dialog);
    enginePathEdit->setObjectName(QStringLiteral("enginePathEdit"));
    enginePathEdit->setText(settings.value(QStringLiteral("engine/path"),
        PikafishEngine::defaultEnginePath()).toString());
    enginePathEdit->setPlaceholderText(PikafishEngine::defaultEnginePath());
    enginePathEdit->setClearButtonEnabled(true);
    auto *chooseEngineButton = new QPushButton(QStringLiteral("选择…"), &dialog);
    chooseEngineButton->setObjectName(QStringLiteral("chooseEngineButton"));
    auto *builtInEngineButton = new QPushButton(QStringLiteral("内置"), &dialog);
    builtInEngineButton->setToolTip(QStringLiteral("恢复使用应用内置的 Pikafish"));
    auto *enginePathRow = new QHBoxLayout;
    enginePathRow->addWidget(enginePathEdit, 1);
    enginePathRow->addWidget(chooseEngineButton);
    enginePathRow->addWidget(builtInEngineButton);
    form->addRow(QStringLiteral("皮卡鱼引擎"), enginePathRow);
    connect(chooseEngineButton, &QPushButton::clicked, &dialog, [&dialog, enginePathEdit]() {
        const QString path = QFileDialog::getOpenFileName(&dialog,
            QStringLiteral("选择 Pikafish 可执行文件"), enginePathEdit->text());
        if (!path.isEmpty())
            enginePathEdit->setText(path);
    });
    connect(builtInEngineButton, &QPushButton::clicked, enginePathEdit, [enginePathEdit]() {
        enginePathEdit->setText(PikafishEngine::defaultEnginePath());
    });
    auto *timeSpin = new QSpinBox(&dialog);
    timeSpin->setRange(500, 60000);
    timeSpin->setSingleStep(500);
    timeSpin->setSuffix(QStringLiteral(" ms"));
    timeSpin->setValue(settings.value(QStringLiteral("engine/moveTime"), 3000).toInt());
    form->addRow(QStringLiteral("单次分析时长"), timeSpin);
    auto *multiPvSpin = new QSpinBox(&dialog);
    multiPvSpin->setRange(1, 5);
    multiPvSpin->setValue(settings.value(QStringLiteral("engine/multiPv"), 3).toInt());
    form->addRow(QStringLiteral("推荐着法数量"), multiPvSpin);
    auto *threadsSpin = new QSpinBox(&dialog);
    threadsSpin->setRange(1, 16);
    threadsSpin->setValue(settings.value(QStringLiteral("engine/threads"), 2).toInt());
    form->addRow(QStringLiteral("搜索线程"), threadsSpin);
    auto *hashSpin = new QSpinBox(&dialog);
    hashSpin->setRange(16, 1024);
    hashSpin->setSingleStep(64);
    hashSpin->setSuffix(QStringLiteral(" MB"));
    hashSpin->setValue(settings.value(QStringLiteral("engine/hash"), 128).toInt());
    form->addRow(QStringLiteral("哈希内存"), hashSpin);
    layout->addLayout(form);
    auto *note = new QLabel(QStringLiteral("分析完全在本地离线运行。\n首次运行若被 macOS 阻止，请在“系统设置 → 隐私与安全性”中允许。"), &dialog);
    note->setProperty("muted", true);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString enginePath = enginePathEdit->text().trimmed();
    const QFileInfo engineFile(enginePath);
    if (enginePath.isEmpty() || !engineFile.isFile() || !engineFile.isExecutable()) {
        QMessageBox::warning(this, QStringLiteral("皮卡鱼路径无效"),
            QStringLiteral("请选择具有执行权限的 Pikafish 可执行文件，或点击“内置”恢复默认版本。"));
        return;
    }
    m_engine->stop();
    m_engine->setEnginePath(engineFile.absoluteFilePath());
    m_engine->setMoveTime(timeSpin->value());
    m_engine->setMultiPv(multiPvSpin->value());
    m_engine->setThreads(threadsSpin->value());
    m_engine->setHash(hashSpin->value());
    settings.setValue(QStringLiteral("engine/path"), engineFile.absoluteFilePath());
    settings.setValue(QStringLiteral("engine/moveTime"), timeSpin->value());
    settings.setValue(QStringLiteral("engine/multiPv"), multiPvSpin->value());
    settings.setValue(QStringLiteral("engine/threads"), threadsSpin->value());
    settings.setValue(QStringLiteral("engine/hash"), hashSpin->value());
    m_engineStatusLabel->setText(QStringLiteral("设置已保存 · 点击开始分析"));
}

void MainWindow::toggleTianTian()
{
    if (m_tianTian->isRunning()) {
        m_tianTian->stop();
        return;
    }

    if (!MacAutomation::hasScreenCaptureAccess() && !MacAutomation::requestScreenCaptureAccess()) {
        QMessageBox::warning(this, QStringLiteral("需要屏幕录制权限"),
            QStringLiteral("请在 macOS“屏幕录制与系统录音”中允许棋析，重新打开应用后再选择对弈窗口。"));
        return;
    }
    WindowPickerDialog picker(this);
    if (picker.exec() != QDialog::Accepted)
        return;
    const MacWindowInfo selectedWindow = picker.selectedWindow();
    if (!selectedWindow.id)
        return;

    QSettings settings;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("自动对弈 · 对局设置"));
    dialog.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *windowName = new QLabel(selectedWindow.displayName(), &dialog);
    windowName->setWordWrap(true);
    windowName->setToolTip(QStringLiteral("窗口位置或尺寸改变后会按最新棋盘四角自动换算点击位置"));
    form->addRow(QStringLiteral("对弈窗口"), windowName);
    auto *sideCombo = new QComboBox(&dialog);
    sideCombo->addItem(QStringLiteral("执红"), true);
    sideCombo->addItem(QStringLiteral("执黑"), false);
    sideCombo->setCurrentIndex(settings.value(QStringLiteral("tiantian/red"), true).toBool() ? 0 : 1);
    form->addRow(QStringLiteral("己方执子"), sideCombo);

    auto *startCombo = new QComboBox(&dialog);
    startCombo->addItem(QStringLiteral("从当前局面继续（轮到己方）"), true);
    startCombo->addItem(QStringLiteral("从初始局面开始"), false);
    startCombo->setCurrentIndex(settings.value(QStringLiteral("tiantian/fromCurrent"), true).toBool() ? 0 : 1);
    form->addRow(QStringLiteral("开始方式"), startCombo);

    auto *directionCombo = new QComboBox(&dialog);
    directionCombo->addItem(QStringLiteral("红方在棋盘下方"), true);
    directionCombo->addItem(QStringLiteral("红方在棋盘上方"), false);
    directionCombo->setCurrentIndex(settings.value(QStringLiteral("tiantian/redBottom"), true).toBool() ? 0 : 1);
    form->addRow(QStringLiteral("棋盘方向"), directionCombo);

    auto *moveTime = new QSpinBox(&dialog);
    moveTime->setRange(200, 60000);
    moveTime->setSingleStep(500);
    moveTime->setSuffix(QStringLiteral(" 毫秒"));
    moveTime->setValue(settings.value(QStringLiteral("tiantian/moveTime"), 1000).toInt());
    form->addRow(QStringLiteral("每手思考"), moveTime);
    layout->addLayout(form);

    auto *hint = new QLabel(QStringLiteral(
        "启动后自动切换到所选窗口，识别棋盘四角和棋子、计算并点击走棋。\n"
        "无需手工校准坐标。运行中再次点击工具栏按钮即可停止。"), &dialog);
    hint->setWordWrap(true);
    hint->setProperty("muted", true);
    layout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始对弈"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    TianTianOptions options;
    options.red = sideCombo->currentData().toBool();
    options.fromCurrent = startCombo->currentData().toBool();
    options.redBottom = directionCombo->currentData().toBool();
    options.moveTimeMs = moveTime->value();
    options.window = selectedWindow;
    settings.setValue(QStringLiteral("tiantian/red"), options.red);
    settings.setValue(QStringLiteral("tiantian/fromCurrent"), options.fromCurrent);
    settings.setValue(QStringLiteral("tiantian/redBottom"), options.redBottom);
    settings.setValue(QStringLiteral("tiantian/moveTime"), options.moveTimeMs);
    m_engine->stop();
    m_tianTian->start(options);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("关于棋析"),
        QStringLiteral("<h2>棋析</h2><p>基于 Qt 6 的中国象棋局势分析工具。</p>"
                       "<p>支持自由摆棋、合法走子、红方视角评分、多候选推荐、逐步变化预览、FEN 导入，以及截图／图片识别棋盘与人工校对。</p>"
                       "<p><b>图像识别：</b>TheOne1006 双 ONNX 模型 + ONNX Runtime，本地离线处理。<br>"
                       "模型权重的再分发授权提醒见 THIRD_PARTY_NOTICES.md。</p>"));
}

void MainWindow::showEngineError(const QString &message)
{
    m_progress->hide();
    m_engineStatusLabel->setText(QStringLiteral("分析需要检查"));
    auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("分析提示"), message,
                                QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
    updateActions();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_tianTian->stop();
    m_engine->shutdown();
    QMainWindow::closeEvent(event);
}
