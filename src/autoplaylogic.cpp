#include "autoplaylogic.h"

void BoardObservationStability::reset()
{
    m_candidate = Position::empty();
    m_hasCandidate = false;
    m_consecutiveFrames = 0;
}

bool BoardObservationStability::observe(const Position &position)
{
    if (!m_hasCandidate || m_candidate.board != position.board) {
        m_candidate = position;
        m_candidate.redTurn = position.redTurn;
        m_hasCandidate = true;
        m_consecutiveFrames = 1;
        return false;
    }
    ++m_consecutiveFrames;
    return m_consecutiveFrames >= 2;
}

bool findSingleLegalTransition(const Position &before, const Position &observed,
                               Move *move, Position *after)
{
    for (const Move &candidate : before.legalMoves()) {
        const Position candidateAfter = before.afterMove(candidate);
        if (candidateAfter.board != observed.board)
            continue;
        if (move)
            *move = candidate;
        if (after)
            *after = candidateAfter;
        return true;
    }
    return false;
}

QPointF boardImagePointForUci(const QString &uciSquare, const QPolygonF &corners,
                              bool redBottom)
{
    if (uciSquare.size() < 2 || corners.size() != 4)
        return {};
    int col = uciSquare.at(0).toLatin1() - 'a';
    int row = 9 - QStringView(uciSquare).mid(1, 1).toInt();
    if (col < 0 || col > 8 || row < 0 || row > 9)
        return {};
    if (!redBottom) {
        row = 9 - row;
        col = 8 - col;
    }
    const double u = col / 8.0;
    const double v = row / 9.0;
    const QPointF top = corners[0] + (corners[1] - corners[0]) * u;
    const QPointF bottom = corners[2] + (corners[3] - corners[2]) * u;
    return top + (bottom - top) * v;
}
