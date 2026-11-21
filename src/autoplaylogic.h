#pragma once

#include "position.h"

#include <QPointF>
#include <QPolygonF>

// Requires two consecutive, reliable observations of the same board. An
// unreliable frame invalidates both a pending candidate and a previously
// accepted observation, so recognition cannot carry stability across a gap.
class BoardObservationStability {
public:
    void reset();
    bool observe(const Position &position);
    int consecutiveFrames() const { return m_consecutiveFrames; }

private:
    Position m_candidate = Position::empty();
    bool m_hasCandidate = false;
    int m_consecutiveFrames = 0;
};

// Legal moves validate an image candidate; they never modify that candidate.
bool findSingleLegalTransition(const Position &before, const Position &observed,
                               Move *move = nullptr, Position *after = nullptr);

// Map a canonical UCI square to visual image coordinates. Corners are always
// the original image's top-left, top-right, bottom-left and bottom-right.
QPointF boardImagePointForUci(const QString &uciSquare, const QPolygonF &corners,
                              bool redBottom);
