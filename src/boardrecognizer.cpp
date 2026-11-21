#include "boardrecognizer.h"

#include "piecedetector.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QtConcurrent/QtConcurrent>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#ifndef RECOGNITION_MODEL_DIR
#define RECOGNITION_MODEL_DIR "third_party/models/theone1006"
#endif
#ifndef RECOGNITION_STYLE_REFERENCE
#define RECOGNITION_STYLE_REFERENCE "resources/recognition/wooden-initial.png"
#endif

namespace {
struct RecognitionResult {
    Position position;
    QVector<double> confidences;
    QString details;
    QString error;
    QPolygonF boardCorners;
    QVector<int> colorCorrectedSquares;

    bool succeeded() const { return error.isEmpty(); }
};

QString modelDirectory()
{
    const QString bundled = QCoreApplication::applicationDirPath()
        + QStringLiteral("/../Resources/recognition");
    if (QFileInfo::exists(bundled + QStringLiteral("/pose.onnx"))
        && QFileInfo::exists(bundled + QStringLiteral("/layout_recognition.onnx")))
        return bundled;
    return QStringLiteral(RECOGNITION_MODEL_DIR);
}

QImage woodenStyleReference()
{
    static const QImage reference = [] {
        const QString bundled = QCoreApplication::applicationDirPath()
            + QStringLiteral("/../Resources/recognition/wooden-initial.png");
        const QString path = QFileInfo(bundled).isFile()
            ? bundled : QStringLiteral(RECOGNITION_STYLE_REFERENCE);
        return QImage(path).convertToFormat(QImage::Format_RGB888);
    }();
    return reference;
}

std::vector<float> imageTensor(const QImage &source, int width, int height)
{
    const QImage image = source.scaled(width, height, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    if (image.isNull())
        throw std::runtime_error("无法将图片转换为 RGB");
    constexpr std::array<float, 3> mean = {123.675F, 116.28F, 103.53F};
    constexpr std::array<float, 3> deviation = {58.395F, 57.12F, 57.375F};
    const int plane = width * height;
    std::vector<float> values(static_cast<size_t>(3 * plane));
    for (int y = 0; y < height; ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const int destination = y * width + x;
            for (int channel = 0; channel < 3; ++channel) {
                values[static_cast<size_t>(channel * plane + destination)] =
                    (static_cast<float>(line[x * 3 + channel]) - mean[channel]) / deviation[channel];
            }
        }
    }
    return values;
}

struct ModelContext {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "XiangqiAnalyzer"};
    Ort::SessionOptions options;
    Ort::Session pose;
    Ort::Session layout;

    ModelContext()
        : pose(makeSession(modelDirectory() + QStringLiteral("/pose.onnx")))
        , layout(makeSession(modelDirectory() + QStringLiteral("/layout_recognition.onnx")))
    {
    }

private:
    Ort::Session makeSession(const QString &path)
    {
        if (!QFileInfo::exists(path))
            throw std::runtime_error("找不到识别模型，请重新构建应用");
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        return Ort::Session(environment, path.toUtf8().constData(), options);
    }
};

Ort::Value runModel(Ort::Session &session, std::vector<float> &input,
                    int width, int height, const std::vector<const char *> &outputNames)
{
    const std::array<int64_t, 4> shape = {1, 3, height, width};
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                        shape.data(), shape.size());
    const char *inputName[] = {"input"};
    auto outputs = session.Run(Ort::RunOptions{nullptr}, inputName, &tensor, 1,
                               outputNames.data(), outputNames.size());
    if (outputs.size() != 1)
        throw std::runtime_error("识别模型输出数量异常");
    return std::move(outputs.front());
}

QImage rectifyBoard(const QImage &source, const std::array<QPointF, 4> &corners)
{
    // Reproduce the upstream pipeline: warp to 450x500 with 50 px margins,
    // then take the centered 400x450 classifier crop.
    const QPolygonF from = {corners[0], corners[1], corners[2], corners[3]};
    const QPolygonF to = {QPointF(50, 50), QPointF(400, 50),
                          QPointF(50, 450), QPointF(400, 450)};
    QTransform transform;
    if (!QTransform::quadToQuad(from, to, transform))
        return {};
    QImage warped(450, 500, QImage::Format_RGB888);
    warped.fill(Qt::black);
    {
        QPainter painter(&warped);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setTransform(transform);
        painter.drawImage(0, 0, source);
    }
    return warped.copy(25, 25, 400, 450);
}

RecognitionResult recognizeImage(const QImage &source, BoardOrientation orientation)
{
    // Keep every glyph upright. A red-top board is normalized by rotating the
    // 90 square results, not the pixels: rotating the whole image would turn
    // every Chinese character upside down and confuse both neural networks.
    RecognitionResult result;
    try {
        if (source.isNull() || source.width() < 90 || source.height() < 100) {
            result.error = QStringLiteral("图片太小，请提供包含完整棋盘的清晰图片。");
            return result;
        }
        static ModelContext models;
        auto poseInput = imageTensor(source, 256, 256);
        const std::array<int64_t, 4> poseShape = {1, 3, 256, 256};
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value poseTensor = Ort::Value::CreateTensor<float>(memory, poseInput.data(), poseInput.size(),
                                                                poseShape.data(), poseShape.size());
        const char *inputName[] = {"input"};
        const char *poseOutputNames[] = {"simcc_x", "simcc_y"};
        auto poseOutputs = models.pose.Run(Ort::RunOptions{nullptr}, inputName, &poseTensor, 1,
                                           poseOutputNames, 2);
        if (poseOutputs.size() != 2)
            throw std::runtime_error("棋盘定位模型输出异常");
        const float *xs = poseOutputs[0].GetTensorMutableData<float>();
        const float *ys = poseOutputs[1].GetTensorMutableData<float>();
        if (poseOutputs[0].GetTensorTypeAndShapeInfo().GetElementCount() < 4 * 512
            || poseOutputs[1].GetTensorTypeAndShapeInfo().GetElementCount() < 4 * 512)
            throw std::runtime_error("棋盘定位模型尺寸异常");

        std::array<QPointF, 4> corners;
        double poseConfidence = 0.0;
        for (int corner = 0; corner < 4; ++corner) {
            const int offset = corner * 512;
            const int xBest = static_cast<int>(std::max_element(xs + offset, xs + offset + 512) - (xs + offset));
            const int yBest = static_cast<int>(std::max_element(ys + offset, ys + offset + 512) - (ys + offset));
            corners[corner] = QPointF(xBest / 2.0 / 256.0 * source.width(),
                                      yBest / 2.0 / 256.0 * source.height());
            poseConfidence += std::sqrt(std::max(0.0F, xs[offset + xBest] * ys[offset + yBest]));
        }
        poseConfidence /= 4.0;
        if (poseConfidence <= 0.15) {
            result.error = QStringLiteral("没有可靠定位到棋盘四角，请截取完整棋盘并尽量保持正对。");
            return result;
        }
        auto distance = [](const QPointF &a, const QPointF &b) {
            return std::hypot(a.x() - b.x(), a.y() - b.y());
        };
        const double meanWidth = (distance(corners[0], corners[1]) + distance(corners[2], corners[3])) / 2.0;
        const double meanHeight = (distance(corners[0], corners[2]) + distance(corners[1], corners[3])) / 2.0;
        const double aspect = meanWidth / std::max(0.001, meanHeight);
        if (meanWidth < 72.0 || meanHeight < 90.0 || aspect <= 0.65 || aspect >= 1.15) {
            result.error = QStringLiteral("棋盘区域比例异常，请确保四个角都在图片内，并尽量正对棋盘截图。");
            return result;
        }

        const bool poseIndicatesRedTop = corners[0].y() > corners[2].y()
            && corners[1].y() > corners[3].y();
        const bool redTopBoard = orientation == BoardOrientation::RedTop
            || (orientation == BoardOrientation::Auto && poseIndicatesRedTop);

        // Pose keypoints follow the board's own orientation. On a red-top board
        // the model can return BR, BL, TR, TL instead of visual TL, TR, BL, BR.
        // Rectifying with that order rotates every upright glyph by 180 degrees.
        // Use visual corners consistently for both recognition and mouse clicks.
        std::sort(corners.begin(), corners.end(), [](const QPointF &first, const QPointF &second) {
            return first.y() < second.y();
        });
        if (corners[0].x() > corners[1].x())
            std::swap(corners[0], corners[1]);
        if (corners[2].x() > corners[3].x())
            std::swap(corners[2], corners[3]);

        result.boardCorners = {corners[0], corners[1], corners[2], corners[3]};
        const QImage boardImage = rectifyBoard(source.convertToFormat(QImage::Format_RGB888), corners);
        if (boardImage.size() != QSize(400, 450)) {
            result.error = QStringLiteral("棋盘四角无法完成透视校正，请换一张更端正、清晰的图片。");
            return result;
        }
        if (qEnvironmentVariableIsSet("XIANGQI_RECOGNITION_DEBUG")) {
            qDebug() << "pose source" << source.size() << "corners" << corners[0] << corners[1]
                     << corners[2] << corners[3] << "confidence" << poseConfidence;
            boardImage.save(QDir::temp().filePath(QStringLiteral("xiangqi-recognition-crop.png")));
        }
        auto layoutInput = imageTensor(boardImage, 315, 280);
        auto output = runModel(models.layout, layoutInput, 315, 280, {"output"});
        if (output.GetTensorTypeAndShapeInfo().GetElementCount() < 90 * 16)
            throw std::runtime_error("棋子识别模型输出尺寸异常");
        const float *values = output.GetTensorMutableData<float>();
        constexpr std::array<char, 16> labels = {'.', 'x', 'K', 'A', 'B', 'N', 'R', 'C', 'P',
                                                 'k', 'a', 'b', 'n', 'r', 'c', 'p'};
        result.position = Position::empty();
        result.confidences.resize(90);
        double confidenceSum = 0.0;
        int lowConfidence = 0;
        int pieceCount = 0;
        std::array<std::array<double, 16>, 90> probabilitiesBySquare{};
        for (int index = 0; index < 90; ++index) {
            const float *logits = values + index * 16;
            float maximum = *std::max_element(logits, logits + 16);
            const float minimum = *std::min_element(logits, logits + 16);
            float rawTotal = 0.0F;
            for (int type = 0; type < 16; ++type)
                rawTotal += logits[type];
            const bool alreadyProbabilities = minimum >= -0.0001F && maximum <= 1.0001F
                && std::abs(rawTotal - 1.0F) < 0.02F;
            std::array<double, 16> probabilities{};
            double total = 0.0;
            for (int type = 0; type < 16; ++type) {
                probabilities[type] = alreadyProbabilities
                    ? std::max(0.0F, logits[type])
                    : std::exp(static_cast<double>(logits[type] - maximum));
                total += probabilities[type];
            }
            const int best = static_cast<int>(std::max_element(logits, logits + 16) - logits);
            const double confidence = probabilities[best] / total;
            for (int type = 0; type < 16; ++type)
                probabilitiesBySquare[index][type] = probabilities[type] / total;
            result.confidences[index] = best == 1 ? 0.0 : confidence;
            confidenceSum += confidence;
            if (best == 1 || confidence < 0.65)
                ++lowConfidence;
            const char piece = best == 1 ? Piece::Empty : labels[best];
            result.position.setPiece(index, piece);
            if (piece != Piece::Empty)
                ++pieceCount;
        }

        const Position layoutPosition = result.position;
        const QVector<double> layoutConfidences = result.confidences;
        const PieceDetectionResult detection = PieceDetector::detect(boardImage);
        PieceGridResult detectedGrid;
        bool detectorPrimary = false;
        if (detection.succeeded()) {
            bool boardDetected = false;
            for (const PieceDetection &piece : detection.detections) {
                if (piece.piece == '0' && piece.confidence >= 0.50) {
                    boardDetected = true;
                    break;
                }
            }
            detectedGrid = PieceDetector::mapToGrid(detection.detections, boardImage.size());
            detectorPrimary = boardDetected;
            if (detectorPrimary && detectedGrid.unmappedPieces > 0) {
                result.error = QStringLiteral("检测到 %1 枚棋子无法对应棋盘交叉点，正在等待更清晰的画面")
                    .arg(detectedGrid.unmappedPieces);
                return result;
            }
            if (detectorPrimary) {
                result.position = detectedGrid.position;
                result.confidences = detectedGrid.confidences;
            }
        }
        if (qEnvironmentVariableIsSet("XIANGQI_RECOGNITION_DEBUG")) {
            qDebug() << "piece detector" << detection.succeeded() << detectorPrimary
                     << "candidates" << detection.candidateCount
                     << "detections" << detection.detections.size()
                     << "duplicates" << detectedGrid.duplicateSquares
                     << "conflicts" << detectedGrid.conflictingSquares
                     << "unmapped" << detectedGrid.unmappedPieces
                     << "error" << detection.error;
        }

        int colorCorrections = 0;
        const double gridMarginX = 25.0 / 400.0;
        const double gridMarginY = 25.0 / 450.0;
        const double gridWidth = boardImage.width() * 350.0 / 400.0;
        const double gridHeight = boardImage.height() * 400.0 / 450.0;
        const double sampleRadius = std::min(gridWidth / 8.0, gridHeight / 9.0) * 0.30;
        auto pieceCenter = [&](int index) {
            return QPointF(gridMarginX * boardImage.width()
                               + Position::colOf(index) * gridWidth / 8.0,
                           gridMarginY * boardImage.height()
                               + Position::rowOf(index) * gridHeight / 9.0);
        };
        auto comparePatches = [](const QImage &imageA, const QPointF &centerA,
                                 const QImage &imageB, const QPointF &centerB) {
            constexpr int radius = 11;
            constexpr int side = radius * 2 + 1;
            constexpr int count = side * side;
            double bestScore = -1.0;
            // Perspective rectification can place equivalent glyphs several
            // pixels apart. Use the best small alignment instead of comparing
            // only exactly coincident pixels.
            for (int shiftY = -6; shiftY <= 6; ++shiftY) {
                for (int shiftX = -6; shiftX <= 6; ++shiftX) {
                    std::array<double, count> a{};
                    std::array<double, count> b{};
                    double meanA = 0.0;
                    double meanB = 0.0;
                    for (int y = -radius; y <= radius; ++y) {
                        for (int x = -radius; x <= radius; ++x) {
                            const int offset = (y + radius) * side + x + radius;
                            const QRgb pixelA = imageA.pixel(qRound(centerA.x()) + x,
                                                             qRound(centerA.y()) + y);
                            const QRgb pixelB = imageB.pixel(qRound(centerB.x()) + x + shiftX,
                                                             qRound(centerB.y()) + y + shiftY);
                            a[offset] = qGray(pixelA);
                            b[offset] = qGray(pixelB);
                            meanA += a[offset];
                            meanB += b[offset];
                        }
                    }
                    meanA /= count;
                    meanB /= count;
                    double dot = 0.0;
                    double normA = 0.0;
                    double normB = 0.0;
                    for (int offset = 0; offset < count; ++offset) {
                        a[offset] -= meanA;
                        b[offset] -= meanB;
                        dot += a[offset] * b[offset];
                        normA += a[offset] * a[offset];
                        normB += b[offset] * b[offset];
                    }
                    bestScore = std::max(bestScore,
                        dot / std::sqrt(std::max(0.0001, normA * normB)));
                }
            }
            return bestScore;
        };
        auto patchSimilarity = [&](int first, int second) {
            return comparePatches(boardImage, pieceCenter(first), boardImage, pieceCenter(second));
        };
        const QImage referenceBoard = woodenStyleReference();
        const Position referencePosition = Position::initial();
        const bool hasReferenceStyle = referenceBoard.size() == boardImage.size();
        for (int index = 0; index < 90; ++index) {
            const char piece = result.position.piece(index);
            const char layoutPiece = layoutPosition.piece(index);
            if (boardImage.isNull()
                || (piece == Piece::Empty && (!detectorPrimary || layoutPiece == Piece::Empty)))
                continue;
            const int row = Position::rowOf(index);
            const int col = Position::colOf(index);
            const double centerX = gridMarginX * boardImage.width() + col * gridWidth / 8.0;
            const double centerY = gridMarginY * boardImage.height() + row * gridHeight / 9.0;
            int redPixels = 0;
            int darkPixels = 0;
            int samples = 0;
            for (int y = std::max(0, static_cast<int>(centerY - sampleRadius));
                 y <= std::min(boardImage.height() - 1, static_cast<int>(centerY + sampleRadius)); ++y) {
                const uchar *line = boardImage.constScanLine(y);
                for (int x = std::max(0, static_cast<int>(centerX - sampleRadius));
                     x <= std::min(boardImage.width() - 1, static_cast<int>(centerX + sampleRadius)); ++x) {
                    if (std::hypot(x - centerX, y - centerY) > sampleRadius)
                        continue;
                    const int r = line[x * 3];
                    const int g = line[x * 3 + 1];
                    const int b = line[x * 3 + 2];
                    ++samples;
                    if (r > 100 && g < 135 && r - g > 45 && r - b > 40)
                        ++redPixels;
                    if (std::max({r, g, b}) < 105 && std::max({r, g, b}) - std::min({r, g, b}) < 55)
                        ++darkPixels;
                }
            }
            const bool redEvidence = samples > 30 && redPixels > samples * 0.08
                && redPixels > darkPixels * 2 && redPixels > 12;
            const bool darkEvidence = samples > 30 && darkPixels > samples * 0.12
                && darkPixels > redPixels * 2 && darkPixels > 18;
            if (qEnvironmentVariableIsSet("XIANGQI_RECOGNITION_DEBUG"))
                qDebug() << "color" << index << piece << "center" << centerX << centerY
                         << "red" << redPixels << "dark" << darkPixels << "samples" << samples;
            const bool detectionDispute = detectedGrid.duplicateSquares.contains(index)
                || detectedGrid.conflictingSquares.contains(index);
            const bool red = Piece::isRed(piece == Piece::Empty ? layoutPiece : piece);
            const bool pixelColorConflict = piece != Piece::Empty
                && ((red && darkEvidence && !redEvidence)
                    || (!red && redEvidence && !darkEvidence));
            const bool strongAgreedDetection = detectorPrimary && piece != Piece::Empty
                && layoutPiece == piece && detectedGrid.confidences[index] >= 0.80
                && !detectionDispute;
            // Positional bias can make both networks assign the top army the
            // wrong color with high confidence. Direct pixel evidence must still
            // be allowed to disagree with that agreement.
            if (strongAgreedDetection && !(redTopBoard && pixelColorConflict))
                continue;
            const bool colorConflict = piece != Piece::Empty
                && (redTopBoard || detectedGrid.confidences[index] < 0.80)
                && ((red && darkEvidence && !redEvidence)
                    || (!red && redEvidence && !darkEvidence));
            const bool needsTypeReview = result.confidences[index] < 0.65;
            const bool layoutDisagreement = detectorPrimary && layoutPiece != piece;
            if (colorConflict || needsTypeReview || layoutDisagreement) {
                // Pixel evidence can establish the color even when the classifier
                // confuses the type. Low-confidence pieces with the correct color
                // need the same glyph check: a red elephant can resemble a cannon.
                const bool correctedRed = colorConflict ? !red : red;
                const int firstType = correctedRed ? 2 : 9;
                const int endType = correctedRed ? 9 : 16;
                int correctedType = firstType;
                double colorTotal = 0.0;
                for (int type = firstType; type < endType; ++type) {
                    const double probability = probabilitiesBySquare[index][type];
                    colorTotal += probability;
                    if (probability > probabilitiesBySquare[index][correctedType])
                        correctedType = type;
                }
                const double modelConfidence = colorTotal > 0.0
                    ? probabilitiesBySquare[index][correctedType] / colorTotal : 0.0;

                // Identical pieces in one screenshot share the same skin. Compare
                // the glyph with confidently classified pieces of the same color;
                // this is especially useful for pieces at the board edge.
                std::array<double, 16> typeSimilarity{};
                typeSimilarity.fill(-1.0);
                for (int other = 0; other < 90; ++other) {
                    const char candidate = result.position.piece(other);
                    if (other == index || candidate == Piece::Empty
                        || Piece::isRed(candidate) != correctedRed
                        || result.confidences[other] < 0.65)
                        continue;
                    const int type = std::find(labels.begin(), labels.end(), candidate) - labels.begin();
                    if (type >= firstType && type < endType)
                        typeSimilarity[type] = std::max(typeSimilarity[type], patchSimilarity(index, other));
                }
                double bestSimilarity = -1.0;
                double secondSimilarity = -1.0;
                int templateType = correctedType;
                for (int type = firstType; type < endType; ++type) {
                    const double similarity = typeSimilarity[type];
                    if (similarity > bestSimilarity) {
                        secondSimilarity = bestSimilarity;
                        bestSimilarity = similarity;
                        templateType = type;
                    } else if (similarity > secondSimilarity) {
                        secondSimilarity = similarity;
                    }
                }
                const bool reliableTemplate = bestSimilarity >= 0.38
                    && bestSimilarity - secondSimilarity >= 0.06;

                // The supplied wooden initial board is rectified to the same
                // 400x450 geometry. It provides independent templates for every
                // piece type and, importantly, for empty intersections.
                std::array<double, 16> referenceSimilarity{};
                referenceSimilarity.fill(-1.0);
                if (hasReferenceStyle) {
                    for (int other = 0; other < 90; ++other) {
                        const char referencePiece = referencePosition.piece(other);
                        const int type = referencePiece == Piece::Empty ? 1
                            : static_cast<int>(std::find(labels.begin(), labels.end(), referencePiece)
                                               - labels.begin());
                        referenceSimilarity[type] = std::max(referenceSimilarity[type],
                            comparePatches(boardImage, pieceCenter(index),
                                           referenceBoard, pieceCenter(other)));
                    }
                }
                double bestReference = -1.0;
                double secondReference = -1.0;
                int referenceType = correctedType;
                for (int type = 1; type < 16; ++type) {
                    const double similarity = referenceSimilarity[type];
                    if (similarity > bestReference) {
                        secondReference = bestReference;
                        bestReference = similarity;
                        referenceType = type;
                    } else if (similarity > secondReference) {
                        secondReference = similarity;
                    }
                }
                const bool reliableReference = bestReference >= 0.50
                    && bestReference - secondReference >= 0.08;
                const bool decisiveReference = redTopBoard && bestReference >= 0.62
                    && bestReference - secondReference >= 0.10;

                bool appliedTemplate = false;
                bool usedReference = false;
                if (decisiveReference) {
                    // A close match to the supplied board's independent glyph
                    // templates can correct a shared positional bias in both
                    // networks. The high score and clear runner-up margin keep
                    // this restricted to genuinely matching piece artwork.
                    correctedType = referenceType;
                    appliedTemplate = true;
                    usedReference = true;
                } else if (reliableReference
                    && (!reliableTemplate || templateType == referenceType)) {
                    correctedType = referenceType;
                    appliedTemplate = true;
                    usedReference = true;
                } else if (reliableTemplate && !reliableReference) {
                    correctedType = templateType;
                    appliedTemplate = true;
                }
                const int detectedType = piece == Piece::Empty ? 1
                    : static_cast<int>(std::find(labels.begin(), labels.end(), piece) - labels.begin());
                if (detectorPrimary) {
                    const double templateScore = (1.0 + (usedReference ? bestReference : bestSimilarity)) / 2.0;
                    const char templatePiece = correctedType == 1 ? Piece::Empty : labels[correctedType];
                    const bool corroboratedAlternative = appliedTemplate
                        && correctedType != detectedType && layoutPiece == templatePiece
                        && layoutConfidences[index] >= 0.65
                        && (detectedGrid.confidences[index] < 0.90 || templateScore >= 0.85);
                    QString decision = QStringLiteral("保留检测结果");
                    if (detectedGrid.conflictingSquares.contains(index)) {
                        // Different detected classes at one intersection are not
                        // resolved by simply taking whichever score is larger.
                        result.confidences[index] = 0.0;
                        decision = QStringLiteral("检测类别冲突");
                    } else if (detectedGrid.duplicateSquares.contains(index)) {
                        result.confidences[index] = std::min(result.confidences[index], 0.60);
                        decision = QStringLiteral("重复检测框");
                    } else if (decisiveReference && correctedType != detectedType) {
                        result.position.setPiece(index, templatePiece);
                        result.confidences[index] = templateScore;
                        decision = QStringLiteral("高匹配样式模板校正共同识别偏差");
                    } else if (appliedTemplate && correctedType == detectedType && !colorConflict) {
                        // Detection and an independent template agree. The old
                        // whole-board classifier no longer decides occupancy.
                        result.confidences[index] = (result.confidences[index] + templateScore) / 2.0;
                        decision = QStringLiteral("检测与模板一致");
                    } else if (corroboratedAlternative) {
                        // A single disagreeing legacy model or template cannot veto
                        // a strong detection. When both independent recognizers
                        // agree, they can correct a systematic YOLO error. At very
                        // high detection confidence, however, require a genuinely
                        // close glyph match: a weak same-board similarity must not
                        // turn a clearly detected edge rook into a pawn.
                        result.position.setPiece(index, templatePiece);
                        if (templateScore >= 0.75 && layoutConfidences[index] >= 0.80) {
                            result.confidences[index] = (templateScore + layoutConfidences[index]) / 2.0;
                            decision = QStringLiteral("整盘模型与模板共同校正检测");
                        } else {
                            result.confidences[index] = 0.60;
                            decision = QStringLiteral("替代结果证据不足");
                        }
                    } else if (piece != Piece::Empty && detectedGrid.confidences[index] >= 0.80
                               && !colorConflict) {
                        decision = QStringLiteral("高置信检测优先");
                    } else if (piece == Piece::Empty && !colorConflict) {
                        // Occupancy comes from detection. An unsupported legacy
                        // non-empty guess must not permanently block an empty square.
                        result.confidences[index] = 0.85;
                        decision = QStringLiteral("检测为空且替代结果未获交叉确认");
                    } else if (layoutPiece == piece && !colorConflict) {
                        if (piece != Piece::Empty) {
                            result.confidences[index] = (result.confidences[index]
                                + layoutConfidences[index]) / 2.0;
                        }
                        decision = QStringLiteral("检测与整盘模型一致");
                    } else {
                        result.confidences[index] = std::min(result.confidences[index], 0.60);
                        decision = QStringLiteral("低置信检测未获确认");
                    }
                    const bool colorResolved = colorConflict && appliedTemplate
                        && templatePiece != Piece::Empty && Piece::isRed(templatePiece) != red;
                    if (colorConflict && !colorResolved) {
                        result.confidences[index] = std::min(result.confidences[index], 0.60);
                        decision = QStringLiteral("低置信检测存在颜色冲突");
                    }
                    if (qEnvironmentVariableIsSet("XIANGQI_RECOGNITION_DEBUG"))
                        qDebug() << "fusion decision" << index << decision;
                } else {
                    if (appliedTemplate) {
                        // Map normalized cross-correlation [-1, 1] to a review score.
                        result.confidences[index] = (1.0 + (usedReference ? bestReference : bestSimilarity)) / 2.0;
                    } else if (colorConflict || (reliableReference && reliableTemplate)) {
                        // Neither conditional model probability nor a disagreement
                        // between two reliable template sets is safe for auto-play.
                        result.confidences[index] = std::min(modelConfidence, 0.60);
                    } else {
                        result.confidences[index] = std::min(result.confidences[index], modelConfidence);
                    }
                    const char correctedPiece = appliedTemplate && correctedType == 1
                        ? Piece::Empty : labels[correctedType];
                    result.position.setPiece(index, correctedPiece);
                }
                const char correctedPiece = result.position.piece(index);
                result.colorCorrectedSquares.append(index);
                if (qEnvironmentVariableIsSet("XIANGQI_RECOGNITION_DEBUG")) {
                    QString candidates;
                    for (int type = firstType; type < endType; ++type) {
                        candidates += QStringLiteral(" %1=%2").arg(labels[type]).arg(
                            colorTotal > 0.0 ? probabilitiesBySquare[index][type] / colorTotal : 0.0);
                    }
                    qDebug() << (colorConflict ? "corrected color" : "reviewed type")
                             << index << correctedPiece
                             << "detector" << piece << "layout" << layoutPiece
                             << "detector-confidence" << detectedGrid.confidences[index]
                             << "layout-confidence" << layoutConfidences[index]
                             << "confidence" << result.confidences[index]
                             << "template" << labels[templateType] << bestSimilarity
                             << "runner-up" << secondSimilarity
                             << "reference" << labels[referenceType] << bestReference
                             << "reference-runner-up" << secondReference
                             << qPrintable(candidates);
                }
                if (colorConflict)
                    ++colorCorrections;
            }
        }
        pieceCount = 0;
        confidenceSum = 0.0;
        for (int square = 0; square < 90; ++square) {
            if (result.position.piece(square) != Piece::Empty)
                ++pieceCount;
            confidenceSum += result.confidences[square];
        }
        lowConfidence = static_cast<int>(result.colorCorrectedSquares.size());
        for (int square = 0; square < result.confidences.size(); ++square) {
            if (result.confidences[square] < 0.65
                && !result.colorCorrectedSquares.contains(square))
                ++lowConfidence;
        }

        auto rotate = [](Position &position, QVector<double> &confidences,
                         QVector<int> &colorCorrectedSquares) {
            Position rotated = Position::empty();
            rotated.redTurn = position.redTurn;
            QVector<double> rotatedConfidence(90);
            QVector<int> rotatedCorrections;
            for (int row = 0; row < 10; ++row) {
                for (int col = 0; col < 9; ++col) {
                    const int from = Position::square(row, col);
                    const int to = Position::square(9 - row, 8 - col);
                    rotated.setPiece(to, position.piece(from));
                    rotatedConfidence[to] = confidences[from];
                }
            }
            for (int from : colorCorrectedSquares) {
                rotatedCorrections.append(Position::square(9 - Position::rowOf(from),
                                                          8 - Position::colOf(from)));
            }
            position = rotated;
            confidences = rotatedConfidence;
            colorCorrectedSquares = rotatedCorrections;
        };
        const bool reversed = redTopBoard;
        if (reversed)
            rotate(result.position, result.confidences, result.colorCorrectedSquares);

        result.details = QStringLiteral("%1 枚棋子 · 平均置信度 %2% · %3 格建议重点校对%4%5\n橙色标记为低置信格或颜色校正版；请同时确认行棋方。")
            .arg(pieceCount)
            .arg(confidenceSum / 90.0 * 100.0, 0, 'f', 1)
            .arg(lowConfidence)
            .arg(reversed ? QStringLiteral(" · 已自动旋转 180°") : QString())
            .arg(colorCorrections > 0 ? QStringLiteral(" · 颜色校正 %1 处").arg(colorCorrections) : QString());
    } catch (const Ort::Exception &error) {
        result.error = QStringLiteral("本地模型推理失败：%1").arg(QString::fromUtf8(error.what()));
    } catch (const std::exception &error) {
        result.error = QStringLiteral("识别失败：%1").arg(QString::fromUtf8(error.what()));
    }
    return result;
}
}

struct BoardRecognizer::Task {
    QFutureWatcher<RecognitionResult> *watcher = nullptr;
};

BoardRecognizer::BoardRecognizer(QObject *parent)
    : QObject(parent)
{
}

BoardRecognizer::~BoardRecognizer()
{
    cancel();
}

void BoardRecognizer::recognize(const QImage &image, BoardOrientation orientation)
{
    cancel();
    auto *task = new Task;
    task->watcher = new QFutureWatcher<RecognitionResult>(this);
    m_task = task;
    connect(task->watcher, &QFutureWatcher<RecognitionResult>::finished, this, [this, task]() {
        if (m_task != task)
            return;
        m_task = nullptr;
        const RecognitionResult result = task->watcher->result();
        task->watcher->deleteLater();
        delete task;
        if (!result.succeeded()) {
            emit recognitionFailed(result.error);
            return;
        }
        emit boardLocated(result.boardCorners);
        emit recognitionFinished(result.position, result.details, result.confidences,
                                 result.colorCorrectedSquares);
    });
    task->watcher->setFuture(QtConcurrent::run([image, orientation]() {
        return recognizeImage(image, orientation);
    }));
}

void BoardRecognizer::cancel()
{
    if (!m_task)
        return;
    Task *task = m_task;
    m_task = nullptr;
    disconnect(task->watcher, nullptr, this, nullptr);
    connect(task->watcher, &QFutureWatcher<RecognitionResult>::finished,
            task->watcher, &QObject::deleteLater);
    delete task;
}

bool BoardRecognizer::isBusy() const
{
    return m_task != nullptr;
}

QString BoardRecognizer::modelDescription()
{
    return QStringLiteral("TheOne1006 双模型 · 本地离线");
}
