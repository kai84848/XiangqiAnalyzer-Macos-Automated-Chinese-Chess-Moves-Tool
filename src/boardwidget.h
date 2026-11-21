#pragma once

#include "position.h"

#include <QPixmap>
#include <QWidget>

class BoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit BoardWidget(QWidget *parent = nullptr);

    void setPosition(const Position &position);
    const Position &position() const { return m_position; }
    void setEditMode(bool enabled);
    bool editMode() const { return m_editMode; }
    void setFlipped(bool flipped);
    bool flipped() const { return m_flipped; }
    void setHintMove(const Move &move);
    void setLastMove(const Move &move) { m_lastMove = move; m_hintMove = {}; update(); }
    void setReviewSquares(const QVector<int> &squares) { m_reviewSquares = squares; update(); }
    void clearReviewSquare(int square) { m_reviewSquares.removeAll(square); update(); }
    QVector<int> reviewSquares() const { return m_reviewSquares; }
    void clearSelection();

    static QPixmap piecePixmap(char piece, int size = 72);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    void moveRequested(const Move &move);
    void editSquareRequested(int square, Qt::MouseButton button);
    void editMoveRequested(const Move &move);
    void selectionChanged(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QPointF pointFor(int square) const;
    int squareAt(const QPointF &point) const;
    void drawBoard(QPainter &painter);
    void drawPieces(QPainter &painter);
    void drawArrow(QPainter &painter, const Move &move);

    Position m_position;
    bool m_editMode = false;
    bool m_flipped = false;
    int m_selected = -1;
    int m_hovered = -1;
    int m_draggedSquare = -1;
    QPointF m_dragPosition;
    Move m_lastMove;
    Move m_hintMove;
    QVector<Move> m_selectedMoves;
    QVector<int> m_reviewSquares;
    double m_cell = 60.0;
    QPointF m_origin;
};
