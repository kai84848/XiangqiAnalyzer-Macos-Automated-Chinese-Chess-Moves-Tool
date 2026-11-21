#include "boardwidget.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QtMath>

namespace {
void paintPiece(QPainter &painter, char piece, const QPointF &center, double radius)
{
    const bool red = Piece::isRed(piece);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(35, 25, 18, 55));
    painter.drawEllipse(center + QPointF(0, radius * 0.10), radius, radius);

    QRadialGradient fill(center - QPointF(radius * 0.3, radius * 0.35), radius * 1.6);
    if (red) {
        fill.setColorAt(0, QColor(QStringLiteral("#d95549")));
        fill.setColorAt(1, QColor(QStringLiteral("#a92f2b")));
    } else {
        fill.setColorAt(0, QColor(QStringLiteral("#4b504e")));
        fill.setColorAt(1, QColor(QStringLiteral("#202522")));
    }
    painter.setBrush(fill);
    painter.setPen(QPen(red ? QColor(QStringLiteral("#8d2825")) : QColor(QStringLiteral("#161a18")),
                        qMax(1.0, radius * 0.062)));
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(QColor(255, 244, 217, 180), qMax(1.0, radius * 0.035)));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius * 0.82, radius * 0.82);

    QFont pieceFont(QStringLiteral("Kaiti SC"));
    pieceFont.setBold(true);
    pieceFont.setPixelSize(qRound(radius * 1.21));
    painter.setPen(QColor(QStringLiteral("#fff4df")));
    painter.setFont(pieceFont);
    painter.drawText(QRectF(center.x() - radius, center.y() - radius * 1.05,
                            radius * 2, radius * 2),
                     Qt::AlignCenter, Piece::displayName(piece));
}
}

BoardWidget::BoardWidget(QWidget *parent)
    : QWidget(parent)
    , m_position(Position::initial())
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("中国象棋棋盘"));
}

void BoardWidget::setPosition(const Position &position)
{
    m_position = position;
    m_selected = -1;
    m_selectedMoves.clear();
    m_draggedSquare = -1;
    update();
}

void BoardWidget::setEditMode(bool enabled)
{
    m_editMode = enabled;
    m_hintMove = {};
    m_draggedSquare = -1;
    clearSelection();
    setCursor(enabled ? Qt::CrossCursor : Qt::PointingHandCursor);
    update();
}

void BoardWidget::setFlipped(bool flipped)
{
    m_flipped = flipped;
    update();
}

void BoardWidget::setHintMove(const Move &move)
{
    m_hintMove = move;
    m_lastMove = {};
    clearSelection();
    update();
}

void BoardWidget::clearSelection()
{
    m_selected = -1;
    m_selectedMoves.clear();
    update();
}

QPixmap BoardWidget::piecePixmap(char piece, int size)
{
    if (piece == Piece::Empty || size <= 0)
        return {};
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    paintPiece(painter, piece, QPointF(size * 0.5, size * 0.47), size * 0.43);
    return pixmap;
}

QSize BoardWidget::minimumSizeHint() const
{
    return {480, 550};
}

QSize BoardWidget::sizeHint() const
{
    return {720, 800};
}

QPointF BoardWidget::pointFor(int square) const
{
    const int row = Position::rowOf(square);
    const int col = Position::colOf(square);
    const int visualRow = m_flipped ? 9 - row : row;
    const int visualCol = m_flipped ? 8 - col : col;
    return m_origin + QPointF(visualCol * m_cell, visualRow * m_cell);
}

int BoardWidget::squareAt(const QPointF &point) const
{
    const double radius = m_cell * 0.43;
    const int visualCol = qRound((point.x() - m_origin.x()) / m_cell);
    const int visualRow = qRound((point.y() - m_origin.y()) / m_cell);
    if (visualRow < 0 || visualRow > 9 || visualCol < 0 || visualCol > 8)
        return -1;
    const QPointF center = m_origin + QPointF(visualCol * m_cell, visualRow * m_cell);
    if (QLineF(center, point).length() > radius)
        return -1;
    const int row = m_flipped ? 9 - visualRow : visualRow;
    const int col = m_flipped ? 8 - visualCol : visualCol;
    return Position::square(row, col);
}

void BoardWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(QStringLiteral("#f4f2ee")));

    const double horizontalMargin = qMax(28.0, width() * 0.055);
    const double verticalMargin = qMax(28.0, height() * 0.035);
    m_cell = qMin((width() - 2 * horizontalMargin) / 8.0,
                  (height() - 2 * verticalMargin) / 9.0);
    m_cell = qMax(30.0, m_cell);
    m_origin = QPointF((width() - 8 * m_cell) / 2.0,
                       (height() - 9 * m_cell) / 2.0);

    drawBoard(painter);
    drawPieces(painter);
    if (m_hintMove.isValid())
        drawArrow(painter, m_hintMove);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(QStringLiteral("#e69a24")), qMax(2.0, m_cell * 0.04)));
    for (int square : std::as_const(m_reviewSquares))
        painter.drawEllipse(pointFor(square), m_cell * 0.46, m_cell * 0.46);

    if (m_selected >= 0) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(QStringLiteral("#278b78")), qMax(2.0, m_cell * 0.045)));
        painter.drawEllipse(pointFor(m_selected), m_cell * 0.48, m_cell * 0.48);
    }
    for (const Move &move : std::as_const(m_selectedMoves)) {
        const QPointF target = pointFor(move.to);
        if (m_position.piece(move.to) == Piece::Empty) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(39, 139, 120, 180));
            painter.drawEllipse(target, m_cell * 0.105, m_cell * 0.105);
        } else {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(39, 139, 120, 210), qMax(2.0, m_cell * 0.04)));
            painter.drawEllipse(target, m_cell * 0.44, m_cell * 0.44);
        }
    }
    if (m_hovered >= 0 && m_hovered != m_selected) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(50, 65, 60, 55), 1.2));
        painter.drawEllipse(pointFor(m_hovered), m_cell * 0.45, m_cell * 0.45);
    }
}

void BoardWidget::drawBoard(QPainter &painter)
{
    const QRectF boardRect(m_origin.x() - m_cell * 0.54, m_origin.y() - m_cell * 0.54,
                           8 * m_cell + m_cell * 1.08, 9 * m_cell + m_cell * 1.08);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(50, 35, 20, 24));
    painter.drawRoundedRect(boardRect.translated(0, 5), m_cell * 0.13, m_cell * 0.13);
    QLinearGradient wood(boardRect.topLeft(), boardRect.bottomRight());
    wood.setColorAt(0, QColor(QStringLiteral("#f3ddb4")));
    wood.setColorAt(0.48, QColor(QStringLiteral("#eace9f")));
    wood.setColorAt(1, QColor(QStringLiteral("#f0d7aa")));
    painter.setBrush(wood);
    painter.drawRoundedRect(boardRect, m_cell * 0.13, m_cell * 0.13);

    painter.save();
    painter.setClipRect(boardRect);
    painter.setPen(QPen(QColor(126, 83, 39, 10), 1));
    for (int i = 0; i < 16; ++i) {
        const double y = boardRect.top() + (i + 0.5) * boardRect.height() / 16;
        QPainterPath grain;
        grain.moveTo(boardRect.left(), y);
        grain.cubicTo(boardRect.left() + boardRect.width() * 0.25, y - 3 + (i % 3),
                      boardRect.left() + boardRect.width() * 0.7, y + 4 - (i % 4),
                      boardRect.right(), y - 1);
        painter.drawPath(grain);
    }
    painter.restore();

    const QColor lineColor(QStringLiteral("#6e4b2d"));
    painter.setPen(QPen(lineColor, qMax(1.0, m_cell * 0.018), Qt::SolidLine, Qt::SquareCap));
    painter.setBrush(Qt::NoBrush);
    const QRectF outer(m_origin - QPointF(m_cell * 0.10, m_cell * 0.10),
                       QSizeF(8 * m_cell + m_cell * 0.20, 9 * m_cell + m_cell * 0.20));
    painter.drawRect(outer);

    for (int row = 0; row < 10; ++row)
        painter.drawLine(pointFor(Position::square(row, 0)), pointFor(Position::square(row, 8)));
    for (int col = 0; col < 9; ++col) {
        painter.drawLine(pointFor(Position::square(0, col)), pointFor(Position::square(4, col)));
        painter.drawLine(pointFor(Position::square(5, col)), pointFor(Position::square(9, col)));
    }
    if (!m_flipped) {
        painter.drawLine(pointFor(Position::square(0, 3)), pointFor(Position::square(2, 5)));
        painter.drawLine(pointFor(Position::square(0, 5)), pointFor(Position::square(2, 3)));
        painter.drawLine(pointFor(Position::square(7, 3)), pointFor(Position::square(9, 5)));
        painter.drawLine(pointFor(Position::square(7, 5)), pointFor(Position::square(9, 3)));
    } else {
        // Palaces remain tied to their actual board squares after flipping.
        painter.drawLine(pointFor(Position::square(0, 3)), pointFor(Position::square(2, 5)));
        painter.drawLine(pointFor(Position::square(0, 5)), pointFor(Position::square(2, 3)));
        painter.drawLine(pointFor(Position::square(7, 3)), pointFor(Position::square(9, 5)));
        painter.drawLine(pointFor(Position::square(7, 5)), pointFor(Position::square(9, 3)));
    }

    auto mark = [&](int row, int col) {
        const QPointF center = pointFor(Position::square(row, col));
        const double gap = m_cell * 0.08;
        const double len = m_cell * 0.16;
        for (int sx : {-1, 1}) {
            if ((col == 0 && sx < 0) || (col == 8 && sx > 0))
                continue;
            for (int sy : {-1, 1}) {
                const QPointF corner = center + QPointF(sx * gap, sy * gap);
                painter.drawLine(corner, corner + QPointF(sx * len, 0));
                painter.drawLine(corner, corner + QPointF(0, sy * len));
            }
        }
    };
    for (int row : {2, 7})
        for (int col : {1, 7})
            mark(row, col);
    for (int row : {3, 6})
        for (int col : {0, 2, 4, 6, 8})
            mark(row, col);

    painter.save();
    painter.setPen(QColor(111, 74, 40, 190));
    QFont riverFont(QStringLiteral("Kaiti SC"));
    riverFont.setPixelSize(qRound(m_cell * 0.42));
    painter.setFont(riverFont);
    const double riverY = m_origin.y() + 4.5 * m_cell;
    const QRectF leftRiver(m_origin.x() + 0.5 * m_cell, riverY - 0.35 * m_cell,
                           3 * m_cell, 0.7 * m_cell);
    const QRectF rightRiver(m_origin.x() + 4.5 * m_cell, riverY - 0.35 * m_cell,
                            3 * m_cell, 0.7 * m_cell);
    painter.drawText(leftRiver, Qt::AlignCenter, m_flipped ? QStringLiteral("汉 界") : QStringLiteral("楚 河"));
    painter.drawText(rightRiver, Qt::AlignCenter, m_flipped ? QStringLiteral("楚 河") : QStringLiteral("汉 界"));
    painter.restore();

    painter.setPen(QColor(95, 75, 54, 150));
    QFont coordinateFont = painter.font();
    coordinateFont.setPixelSize(qMax(9, qRound(m_cell * 0.15)));
    painter.setFont(coordinateFont);
    for (int visualCol = 0; visualCol < 9; ++visualCol) {
        const int actualCol = m_flipped ? 8 - visualCol : visualCol;
        const QString label = m_flipped ? QString::number(9 - actualCol)
                                        : QStringLiteral("九八七六五四三二一").mid(8 - actualCol, 1);
        const QPointF p = m_origin + QPointF(visualCol * m_cell, 9 * m_cell + m_cell * 0.43);
        painter.drawText(QRectF(p.x() - m_cell * 0.3, p.y() - m_cell * 0.15,
                                m_cell * 0.6, m_cell * 0.3), Qt::AlignCenter, label);
    }
}

void BoardWidget::drawPieces(QPainter &painter)
{
    const double radius = m_cell * 0.405;
    for (int square = 0; square < 90; ++square) {
        if (square == m_draggedSquare)
            continue;
        const char p = m_position.piece(square);
        if (p == Piece::Empty)
            continue;
        const QPointF center = pointFor(square);
        if (!m_editMode && m_position.isInCheck(Piece::isRed(p))
            && Piece::typeOf(p) == Piece::King) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(210, 55, 45, 45));
            painter.drawEllipse(center, radius * 1.28, radius * 1.28);
        }
        if (m_lastMove.isValid() && (square == m_lastMove.from || square == m_lastMove.to)) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(230, 166, 45, 65));
            painter.drawEllipse(center, radius * 1.16, radius * 1.16);
        }
        paintPiece(painter, p, center, radius);
    }
    if (m_draggedSquare >= 0) {
        const char piece = m_position.piece(m_draggedSquare);
        if (piece != Piece::Empty) {
            painter.save();
            painter.setOpacity(0.92);
            paintPiece(painter, piece, m_dragPosition, radius);
            painter.restore();
        }
    }
}

void BoardWidget::drawArrow(QPainter &painter, const Move &move)
{
    const QPointF start = pointFor(move.from);
    const QPointF end = pointFor(move.to);
    const double dx = end.x() - start.x();
    const double dy = end.y() - start.y();
    const double distance = std::hypot(dx, dy);
    if (distance < 1.0)
        return;

    // Compute both endpoints from the original vector. This avoids cumulative
    // shortening errors and keeps diagonal and one-step moves equally clear.
    const QPointF unit(dx / distance, dy / distance);
    const QPointF normal(-unit.y(), unit.x());
    const bool shortMove = distance < m_cell * 1.5;
    const double startOffset = m_cell * (shortMove ? 0.37 : 0.45);
    const double endOffset = m_cell * (shortMove ? 0.43 : 0.49);
    const double headLength = m_cell * (shortMove ? 0.15 : 0.23);
    const double headWidth = m_cell * (shortMove ? 0.09 : 0.13);
    const QPointF lineStart = start + unit * startOffset;
    const QPointF arrowTip = end - unit * endOffset;
    const QPointF arrowBase = arrowTip - unit * headLength;
    const QColor color(19, 157, 130, 235);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 250, 229, 220), qMax(5.0, m_cell * 0.12),
                        Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(lineStart, arrowBase);
    painter.setPen(QPen(color, qMax(3.5, m_cell * 0.072), Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(lineStart, arrowBase);

    QPolygonF head;
    head << arrowTip << arrowBase + normal * headWidth << arrowBase - normal * headWidth;
    painter.setPen(QPen(QColor(255, 250, 229, 230), qMax(1.0, m_cell * 0.018),
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(color);
    painter.drawPolygon(head);

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(lineStart, m_cell * 0.045, m_cell * 0.045);

    // The destination ring makes the exact target intersection unambiguous,
    // including captures where the arrowhead must stop outside the piece.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, qMax(2.0, m_cell * 0.032)));
    painter.drawEllipse(end, m_cell * 0.455, m_cell * 0.455);
}

void BoardWidget::mousePressEvent(QMouseEvent *event)
{
    const int square = squareAt(event->position());
    if (m_editMode) {
        if (event->button() == Qt::MiddleButton) {
            if (square >= 0 && m_position.piece(square) != Piece::Empty) {
                m_draggedSquare = square;
                m_dragPosition = event->position();
                m_hovered = -1;
                setCursor(Qt::ClosedHandCursor);
                update();
                event->accept();
            }
            return;
        }
        if (m_draggedSquare >= 0)
            return;
        if (square >= 0)
            emit editSquareRequested(square, event->button());
        return;
    }
    if (square < 0 || event->button() != Qt::LeftButton)
        return;
    const char p = m_position.piece(square);
    if (m_selected >= 0) {
        for (const Move &move : std::as_const(m_selectedMoves)) {
            if (move.to == square) {
                m_lastMove = move;
                m_hintMove = {};
                clearSelection();
                emit moveRequested(move);
                return;
            }
        }
    }
    if (p != Piece::Empty && Piece::isRed(p) == m_position.redTurn) {
        m_selected = square;
        m_selectedMoves = m_position.legalMovesFrom(square);
        m_hintMove = {};
        emit selectionChanged(QStringLiteral("%1 · %2 个合法落点")
                                  .arg(Piece::displayName(p)).arg(m_selectedMoves.size()));
    } else {
        clearSelection();
        emit selectionChanged({});
    }
    update();
}

void BoardWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int square = squareAt(event->position());
    if (m_draggedSquare >= 0) {
        m_dragPosition = event->position();
        m_hovered = square;
        update();
        event->accept();
        return;
    }
    if (square != m_hovered) {
        m_hovered = square;
        update();
    }
}

void BoardWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_editMode || m_draggedSquare < 0 || event->button() != Qt::MiddleButton)
        return;
    const int from = m_draggedSquare;
    const int to = squareAt(event->position());
    m_draggedSquare = -1;
    m_hovered = to;
    setCursor(Qt::CrossCursor);
    update();
    if (to >= 0 && to != from && m_position.piece(to) == Piece::Empty)
        emit editMoveRequested({from, to});
    event->accept();
}

void BoardWidget::leaveEvent(QEvent *)
{
    m_hovered = -1;
    update();
}
