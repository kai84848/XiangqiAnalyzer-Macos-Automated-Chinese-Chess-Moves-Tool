#include "piecedetector.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

#ifndef PIECE_DETECTION_MODEL
#define PIECE_DETECTION_MODEL "third_party/models/atopx/large.onnx"
#endif

namespace {
constexpr int inputSize = 640;
constexpr int outputWidth = 20;
constexpr double candidateThreshold = 0.20;
constexpr std::array<char, 15> labels = {
    'n', 'b', 'a', 'k', 'r', 'c', 'p',
    'R', 'N', 'A', 'K', 'B', 'C', 'P', '0'
};

QString modelPath()
{
    const QString bundled = QCoreApplication::applicationDirPath()
        + QStringLiteral("/../Resources/recognition/piece_detection.onnx");
    return QFileInfo(bundled).isFile() ? bundled : QStringLiteral(PIECE_DETECTION_MODEL);
}

struct DetectorContext {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "XiangqiPieceDetector"};
    Ort::SessionOptions options;
    Ort::Session session;

    DetectorContext()
        : session(makeSession())
    {
    }

private:
    Ort::Session makeSession()
    {
        const QString path = modelPath();
        if (!QFileInfo(path).isFile())
            throw std::runtime_error("找不到棋子检测模型，请重新构建应用");
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        return Ort::Session(environment, path.toUtf8().constData(), options);
    }
};

std::vector<float> inputTensor(const QImage &source)
{
    const QImage image = source.scaled(inputSize, inputSize, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB888);
    if (image.isNull())
        throw std::runtime_error("无法将检测图片转换为 RGB");
    constexpr int plane = inputSize * inputSize;
    std::vector<float> values(static_cast<size_t>(3 * plane));
    for (int y = 0; y < inputSize; ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < inputSize; ++x) {
            const int destination = y * inputSize + x;
            for (int channel = 0; channel < 3; ++channel) {
                values[static_cast<size_t>(channel * plane + destination)] =
                    static_cast<float>(line[x * 3 + channel]) / 255.0F;
            }
        }
    }
    return values;
}

double intersectionOverUnion(const QRectF &first, const QRectF &second)
{
    const QRectF intersection = first.intersected(second);
    const double intersectionArea = std::max(0.0, intersection.width())
        * std::max(0.0, intersection.height());
    const double unionArea = first.width() * first.height()
        + second.width() * second.height() - intersectionArea;
    return unionArea > 0.0 ? intersectionArea / unionArea : 0.0;
}
}

PieceDetectionResult PieceDetector::detect(const QImage &source)
{
    PieceDetectionResult result;
    try {
        if (source.isNull() || source.width() < 20 || source.height() < 20)
            throw std::runtime_error("检测图片为空或太小");
        static DetectorContext context;
        auto input = inputTensor(source);
        const std::array<int64_t, 4> inputShape = {1, 3, inputSize, inputSize};
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                            inputShape.data(), inputShape.size());
        const char *inputNames[] = {"images"};
        const char *outputNames[] = {"output"};
        auto outputs = context.session.Run(Ort::RunOptions{nullptr}, inputNames, &tensor, 1,
                                           outputNames, 1);
        if (outputs.size() != 1)
            throw std::runtime_error("棋子检测模型输出数量异常");
        const auto info = outputs.front().GetTensorTypeAndShapeInfo();
        const auto shape = info.GetShape();
        const size_t elementCount = info.GetElementCount();
        if (elementCount == 0 || elementCount % outputWidth != 0 || shape.size() != 3)
            throw std::runtime_error("棋子检测模型输出尺寸异常");

        const float *values = outputs.front().GetTensorMutableData<float>();
        const bool channelsFirst = shape[1] == outputWidth;
        const size_t rows = channelsFirst ? static_cast<size_t>(shape[2])
                                          : static_cast<size_t>(shape[1]);
        if ((!channelsFirst && shape[2] != outputWidth) || rows == 0
            || elementCount != rows * outputWidth) {
            throw std::runtime_error("不支持的棋子检测模型输出布局");
        }
        auto valueAt = [&](size_t row, size_t column) -> float {
            return channelsFirst ? values[column * rows + row]
                                 : values[row * outputWidth + column];
        };

        struct Candidate {
            QRectF box;
            int type = 0;
            double confidence = 0.0;
        };
        QVector<Candidate> candidates;
        for (size_t row = 0; row < rows; ++row) {
            int bestType = 0;
            float bestClass = -1.0F;
            for (int type = 0; type < 15; ++type) {
                const float probability = valueAt(row, static_cast<size_t>(5 + type));
                if (probability > bestClass) {
                    bestClass = probability;
                    bestType = type;
                }
            }
            const double confidence = static_cast<double>(valueAt(row, 4)) * bestClass;
            if (!std::isfinite(confidence) || confidence < candidateThreshold)
                continue;
            const double centerX = valueAt(row, 0) * source.width() / inputSize;
            const double centerY = valueAt(row, 1) * source.height() / inputSize;
            const double width = valueAt(row, 2) * source.width() / inputSize;
            const double height = valueAt(row, 3) * source.height() / inputSize;
            if (width <= 0.0 || height <= 0.0 || !std::isfinite(width + height + centerX + centerY))
                continue;
            candidates.append({QRectF(centerX - width / 2.0, centerY - height / 2.0,
                                      width, height), bestType, confidence});
        }
        result.candidateCount = static_cast<int>(candidates.size());
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &first, const Candidate &second) {
            return first.confidence > second.confidence;
        });
        QVector<bool> suppressed(candidates.size(), false);
        for (int index = 0; index < candidates.size(); ++index) {
            if (suppressed[index])
                continue;
            const Candidate &candidate = candidates[index];
            result.detections.append({candidate.box, labels[candidate.type], candidate.confidence});
            for (int other = index + 1; other < candidates.size(); ++other) {
                if (!suppressed[other]
                    && intersectionOverUnion(candidate.box, candidates[other].box) > 0.5) {
                    suppressed[other] = true;
                }
            }
        }
    } catch (const Ort::Exception &error) {
        result.error = QStringLiteral("棋子检测模型推理失败：%1").arg(QString::fromUtf8(error.what()));
    } catch (const std::exception &error) {
        result.error = QStringLiteral("棋子检测失败：%1").arg(QString::fromUtf8(error.what()));
    }
    return result;
}

PieceGridResult PieceDetector::mapToGrid(const QVector<PieceDetection> &detections,
                                         const QSize &boardSize)
{
    PieceGridResult result;
    if (boardSize.width() <= 0 || boardSize.height() <= 0)
        return result;
    const double marginX = boardSize.width() * 25.0 / 400.0;
    const double marginY = boardSize.height() * 25.0 / 450.0;
    const double gridWidth = boardSize.width() * 350.0 / 400.0;
    const double gridHeight = boardSize.height() * 400.0 / 450.0;
    for (const PieceDetection &detection : detections) {
        if (detection.piece == '0')
            continue;
        if (detection.confidence < 0.35) {
            ++result.ignoredLowConfidence;
            continue;
        }
        const QPointF center = detection.box.center();
        const double gridX = (center.x() - marginX) * 8.0 / gridWidth;
        const double gridY = (center.y() - marginY) * 9.0 / gridHeight;
        const int col = qRound(gridX);
        const int row = qRound(gridY);
        if (col < 0 || col > 8 || row < 0 || row > 9
            || std::abs(gridX - col) > 0.46 || std::abs(gridY - row) > 0.46) {
            ++result.unmappedPieces;
            continue;
        }
        const int square = Position::square(row, col);
        const char previous = result.position.piece(square);
        if (previous == Piece::Empty) {
            result.position.setPiece(square, detection.piece);
            result.confidences[square] = detection.confidence;
            continue;
        }
        if (previous == detection.piece) {
            if (!result.duplicateSquares.contains(square))
                result.duplicateSquares.append(square);
            result.confidences[square] = std::min(0.60,
                std::max(result.confidences[square], detection.confidence));
            continue;
        }
        if (!result.conflictingSquares.contains(square))
            result.conflictingSquares.append(square);
        if (detection.confidence > result.confidences[square])
            result.position.setPiece(square, detection.piece);
        result.confidences[square] = 0.0;
    }
    return result;
}

QString PieceDetector::modelDescription()
{
    return QStringLiteral("atopx YOLO 棋子检测 · 本地 ONNX");
}
