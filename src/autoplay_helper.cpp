#include "boardrecognizer.h"
#include "pikafishengine.h"
#include "piecedetector.h"
#include "position.h"

#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <iostream>

namespace {
int fail(const QString &message)
{
    const QJsonObject result{{QStringLiteral("ok"), false},
                             {QStringLiteral("error"), message}};
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
    return 2;
}

int succeed(QJsonObject result)
{
    result.insert(QStringLiteral("ok"), true);
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
    return 0;
}

bool parsePosition(const QString &fen, Position &position)
{
    QString error;
    if (!Position::fromFen(fen, position, &error))
        return false;
    return true;
}

int normalizeImage(const QString &path, const QString &outputPath)
{
    QImageReader reader(path);
    reader.setAutoTransform(false);
    const QImage decoded = reader.read();
    if (decoded.isNull())
        return fail(QStringLiteral("无法完整解码图片：%1").arg(reader.errorString()));
    const QImage rgb = decoded.convertToFormat(QImage::Format_RGB888);
    if (rgb.isNull() || !rgb.save(outputPath, "PNG"))
        return fail(QStringLiteral("无法保存标准 RGB PNG"));
    QImageReader verification(outputPath);
    const QImage verified = verification.read();
    if (verified.isNull())
        return fail(QStringLiteral("保存后的 PNG 未通过完整解码验证"));
    return succeed({{QStringLiteral("width"), verified.width()},
                    {QStringLiteral("height"), verified.height()}});
}

int recognize(const QString &path)
{
    QImage image(path);
    if (image.isNull())
        return fail(QStringLiteral("无法读取截图：%1").arg(path));

    BoardRecognizer recognizer;
    int exitCode = 2;
    QObject::connect(&recognizer, &BoardRecognizer::recognitionFailed, &recognizer,
                     [&](const QString &message) {
        exitCode = fail(message);
        QCoreApplication::quit();
    });
    QObject::connect(&recognizer, &BoardRecognizer::recognitionFinished, &recognizer,
                     [&](const Position &position, const QString &details,
                         const QVector<double> &confidences,
                         const QVector<int> &colorCorrectedSquares) {
        QJsonArray confidenceValues;
        QJsonArray correctedValues;
        for (int square : colorCorrectedSquares)
            correctedValues.append(square);
        double minimumPieceConfidence = 1.0;
        int lowConfidence = 0;
        for (int square = 0; square < 90; ++square) {
            const double confidence = square < confidences.size() ? confidences[square] : 0.0;
            confidenceValues.append(confidence);
            if (confidence < 0.65)
                ++lowConfidence;
            if (position.piece(square) != Piece::Empty)
                minimumPieceConfidence = qMin(minimumPieceConfidence, confidence);
        }
        exitCode = succeed({{QStringLiteral("fen"), position.toFen()},
                            {QStringLiteral("details"), details},
                            {QStringLiteral("low_confidence"), lowConfidence},
                            {QStringLiteral("minimum_piece_confidence"), minimumPieceConfidence},
                            {QStringLiteral("color_corrected_squares"), correctedValues},
                            {QStringLiteral("confidences"), confidenceValues}});
        QCoreApplication::quit();
    });
    recognizer.recognize(image);
    QTimer::singleShot(30000, &recognizer, []() {
        fail(QStringLiteral("棋盘识别超时"));
        QCoreApplication::quit();
    });
    return QCoreApplication::exec() == 0 ? exitCode : exitCode;
}

int detect(const QString &path)
{
    const QImage image(path);
    if (image.isNull())
        return fail(QStringLiteral("无法读取截图：%1").arg(path));
    const PieceDetectionResult result = PieceDetector::detect(image);
    if (!result.succeeded())
        return fail(result.error);
    QJsonArray detections;
    for (const PieceDetection &detection : result.detections) {
        detections.append(QJsonObject{
            {QStringLiteral("piece"), QString(QChar(detection.piece))},
            {QStringLiteral("confidence"), detection.confidence},
            {QStringLiteral("x"), detection.box.x()},
            {QStringLiteral("y"), detection.box.y()},
            {QStringLiteral("width"), detection.box.width()},
            {QStringLiteral("height"), detection.box.height()}
        });
    }
    const PieceGridResult grid = PieceDetector::mapToGrid(result.detections, image.size());
    QJsonArray duplicates;
    for (int square : grid.duplicateSquares)
        duplicates.append(square);
    QJsonArray conflicts;
    for (int square : grid.conflictingSquares)
        conflicts.append(square);
    return succeed({{QStringLiteral("candidate_count"), result.candidateCount},
                    {QStringLiteral("detections"), detections},
                    {QStringLiteral("grid_fen"), grid.position.toFen()},
                    {QStringLiteral("duplicate_squares"), duplicates},
                    {QStringLiteral("conflicting_squares"), conflicts},
                    {QStringLiteral("unmapped_pieces"), grid.unmappedPieces}});
}

int analyze(const QString &fen, int moveTimeMs)
{
    Position position;
    if (!parsePosition(fen, position))
        return fail(QStringLiteral("FEN 无效"));

    PikafishEngine engine;
    engine.setMoveTime(moveTimeMs);
    int exitCode = 2;
    QObject::connect(&engine, &PikafishEngine::errorOccurred, &engine,
                     [&](const QString &message) {
        exitCode = fail(message);
        QCoreApplication::quit();
    });
    QObject::connect(&engine, &PikafishEngine::analysisFinished, &engine,
                     [&](const Move &bestMove, const QString &notation, bool) {
        if (!bestMove.isValid()) {
            exitCode = succeed({{QStringLiteral("bestmove"), QJsonValue::Null},
                                {QStringLiteral("notation"), notation}});
            QCoreApplication::quit();
            return;
        }
        const Position after = position.afterMove(bestMove);
        exitCode = succeed({{QStringLiteral("bestmove"), Position::moveToUci(bestMove)},
                            {QStringLiteral("notation"), notation},
                            {QStringLiteral("after_fen"), after.toFen()}});
        QCoreApplication::quit();
    });
    engine.analyze(position, false);
    QTimer::singleShot(moveTimeMs + 20000, &engine, []() {
        fail(QStringLiteral("本地分析超时"));
        QCoreApplication::quit();
    });
    return QCoreApplication::exec() == 0 ? exitCode : exitCode;
}

int apply(const QString &fen, const QString &uci)
{
    Position position;
    if (!parsePosition(fen, position))
        return fail(QStringLiteral("FEN 无效"));
    const Move move = Position::moveFromUci(uci);
    if (!move.isValid() || !position.isLegalMove(move))
        return fail(QStringLiteral("走法不合法：%1").arg(uci));
    return succeed({{QStringLiteral("move"), uci},
                    {QStringLiteral("after_fen"), position.afterMove(move).toFen()}});
}

int transition(const QString &beforeFen, const QString &observedFen)
{
    Position before;
    Position observed;
    if (!parsePosition(beforeFen, before) || !parsePosition(observedFen, observed))
        return fail(QStringLiteral("FEN 无效"));
    for (const Move &move : before.legalMoves()) {
        const Position after = before.afterMove(move);
        if (after.board == observed.board) {
            return succeed({{QStringLiteral("move"), Position::moveToUci(move)},
                            {QStringLiteral("after_fen"), after.toFen()}});
        }
    }
    return fail(QStringLiteral("棋盘变化不是一步合法走法"));
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() < 2) {
        return fail(QStringLiteral("用法：recognize IMAGE | detect IMAGE | analyze FEN MS | apply FEN UCI | transition BEFORE OBSERVED | normalize IMAGE OUTPUT"));
    }
    const QString command = arguments.at(1);
    if (command == QStringLiteral("recognize") && arguments.size() == 3)
        return recognize(arguments.at(2));
    if (command == QStringLiteral("detect") && arguments.size() == 3)
        return detect(arguments.at(2));
    if (command == QStringLiteral("analyze") && arguments.size() == 4)
        return analyze(arguments.at(2), arguments.at(3).toInt());
    if (command == QStringLiteral("apply") && arguments.size() == 4)
        return apply(arguments.at(2), arguments.at(3));
    if (command == QStringLiteral("transition") && arguments.size() == 4)
        return transition(arguments.at(2), arguments.at(3));
    if (command == QStringLiteral("normalize") && arguments.size() == 4)
        return normalizeImage(arguments.at(2), arguments.at(3));
    return fail(QStringLiteral("命令或参数无效"));
}
