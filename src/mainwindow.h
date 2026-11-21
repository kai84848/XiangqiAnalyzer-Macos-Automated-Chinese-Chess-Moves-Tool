#pragma once

#include "pikafishengine.h"
#include "position.h"

#include <QMainWindow>
#include <QVector>

class BoardWidget;
class EvaluationBar;
class QAction;
class QButtonGroup;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class QTabWidget;
class TianTianController;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newGame();
    void toggleEditMode();
    void editSquare(int square, Qt::MouseButton button);
    void clearBoard();
    void startAnalysis();
    void stopAnalysis();
    void requestEngineMove();
    void onMoveRequested(const Move &move);
    void onAnalysisUpdated(const QVector<EngineLine> &lines, bool redToMove);
    void onAnalysisFinished(const Move &bestMove, const QString &notation, bool playMove);
    void showSelectedLine(int row);
    void previewVariation(int lineIndex, int ply);
    void playSelectedLine(int row, int column);
    void undo();
    void redo();
    void navigateToPly(int ply);
    void importFen();
    void recognizeBoard();
    void copyFen();
    void copyMoves();
    void showSettings();
    void toggleTianTian();
    void showAbout();

private:
    void buildUi();
    void buildMenus();
    void applyStyle();
    void updatePositionUi();
    void updateMoveTable();
    void updateActions();
    void resetHistory();
    void commitMove(const Move &move);
    void resetAnalysisDisplay();
    void showEngineError(const QString &message);
    QString scoreText(int redCp, int redMate) const;
    QString assessmentText(int redCp, int redMate) const;
    void updateVariations();
    QString pvText(const EngineLine &line, int lineIndex) const;

    BoardWidget *m_board = nullptr;
    PikafishEngine *m_engine = nullptr;
    TianTianController *m_tianTian = nullptr;
    Position m_position;
    QVector<Position> m_history;
    QStringList m_moveNotations;
    int m_historyIndex = -1;
    QVector<EngineLine> m_lines;
    bool m_linesRedToMove = true;
    char m_palettePiece = Piece::Rook;
    bool m_updatingUi = false;

    QComboBox *m_turnCombo = nullptr;
    QAction *m_editAction = nullptr;
    QAction *m_autoAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_flipAction = nullptr;
    QPushButton *m_analyzeButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_engineMoveButton = nullptr;
    QPushButton *m_tianTianButton = nullptr;
    QPushButton *m_selectedMoveButton = nullptr;
    QWidget *m_editPanel = nullptr;
    QButtonGroup *m_paletteGroup = nullptr;
    QLabel *m_positionLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_scoreLabel = nullptr;
    QLabel *m_assessmentLabel = nullptr;
    QLabel *m_depthLabel = nullptr;
    QLabel *m_engineStatusLabel = nullptr;
    EvaluationBar *m_evaluationBar = nullptr;
    QProgressBar *m_progress = nullptr;
    QTableWidget *m_candidateTable = nullptr;
    QTextBrowser *m_variationText = nullptr;
    QTableWidget *m_moveTable = nullptr;
    QTabWidget *m_sideTabs = nullptr;
    QLineEdit *m_fenEdit = nullptr;
};
