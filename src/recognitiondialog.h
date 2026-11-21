#pragma once

#include "position.h"

#include <QDialog>
#include <QImage>

class BoardRecognizer;
class BoardWidget;
class QButtonGroup;
class QComboBox;
class QLabel;
class QProcess;
class QProgressBar;
class QPushButton;

class RecognitionDialog : public QDialog {
    Q_OBJECT
public:
    explicit RecognitionDialog(QWidget *parent = nullptr);

    void loadImage(const QImage &image, bool recognize = true);

signals:
    void positionAccepted(const Position &position);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void importImage();
    bool loadImageFile(const QString &path);
    void captureScreen();
    void updateSourcePreview();
    void setBusy(bool busy);
    void setStatus(const QString &text, bool error = false);
    void rotateRecognizedBoard();

    QImage m_sourceImage;
    Position m_position;
    BoardRecognizer *m_recognizer = nullptr;
    QLabel *m_sourceLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_captureButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_recognizeButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    BoardWidget *m_board = nullptr;
    QComboBox *m_turnCombo = nullptr;
    QButtonGroup *m_paletteGroup = nullptr;
    QProcess *m_captureProcess = nullptr;
    QWidget *m_captureMainWindow = nullptr;
    char m_palettePiece = Piece::Rook;
};
