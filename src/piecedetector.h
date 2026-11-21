#pragma once

#include "position.h"

#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>

struct PieceDetection {
    QRectF box;
    char piece = Piece::Empty;
    double confidence = 0.0;
};

struct PieceDetectionResult {
    QVector<PieceDetection> detections;
    QString error;
    int candidateCount = 0;

    bool succeeded() const { return error.isEmpty(); }
};

struct PieceGridResult {
    Position position = Position::empty();
    QVector<double> confidences = QVector<double>(90, 1.0);
    QVector<int> duplicateSquares;
    QVector<int> conflictingSquares;
    int ignoredLowConfidence = 0;
    int unmappedPieces = 0;
};

class PieceDetector {
public:
    static PieceDetectionResult detect(const QImage &image);
    static PieceGridResult mapToGrid(const QVector<PieceDetection> &detections,
                                     const QSize &boardSize);
    static QString modelDescription();
};
