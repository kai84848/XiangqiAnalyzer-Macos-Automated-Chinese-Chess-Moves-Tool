#include "position.h"

#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace Piece {
bool isRed(char piece)
{
    return piece >= 'A' && piece <= 'Z';
}

bool isBlack(char piece)
{
    return piece >= 'a' && piece <= 'z';
}

bool sameColor(char a, char b)
{
    return a != Empty && b != Empty
        && ((isRed(a) && isRed(b)) || (isBlack(a) && isBlack(b)));
}

char typeOf(char piece)
{
    if (piece >= 'a' && piece <= 'z')
        return static_cast<char>(piece - 'a' + 'A');
    return piece;
}

char make(bool red, char type)
{
    if (red)
        return type;
    return static_cast<char>(type - 'A' + 'a');
}

QString displayName(char piece)
{
    if (piece == Empty)
        return {};
    const bool red = isRed(piece);
    switch (typeOf(piece)) {
    case King: return red ? QStringLiteral("帅") : QStringLiteral("将");
    case Advisor: return red ? QStringLiteral("仕") : QStringLiteral("士");
    case Elephant: return red ? QStringLiteral("相") : QStringLiteral("象");
    case Horse: return QStringLiteral("马");
    case Rook: return QStringLiteral("车");
    case Cannon: return QStringLiteral("炮");
    case Pawn: return red ? QStringLiteral("兵") : QStringLiteral("卒");
    default: return {};
    }
}
}

Position::Position()
{
    clear();
}

Position Position::initial()
{
    Position position;
    fromFen(QStringLiteral("rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1"), position);
    return position;
}

Position Position::empty()
{
    return Position();
}

void Position::clear()
{
    board.fill(Piece::Empty);
    redTurn = true;
    halfmoveClock = 0;
    fullmoveNumber = 1;
}

void Position::setPiece(int squareIndex, char piece)
{
    if (squareIndex >= 0 && squareIndex < 90)
        board[static_cast<size_t>(squareIndex)] = piece;
}

char Position::piece(int squareIndex) const
{
    if (squareIndex < 0 || squareIndex >= 90)
        return Piece::Empty;
    return board[static_cast<size_t>(squareIndex)];
}

char Position::piece(int row, int col) const
{
    if (row < 0 || row >= 10 || col < 0 || col >= 9)
        return Piece::Empty;
    return piece(square(row, col));
}

Position Position::afterMove(const Move &move) const
{
    Position result = *this;
    result.applyMove(move);
    return result;
}

void Position::applyMove(const Move &move)
{
    if (!move.isValid())
        return;
    const char moving = piece(move.from);
    const char captured = piece(move.to);
    board[static_cast<size_t>(move.to)] = moving;
    board[static_cast<size_t>(move.from)] = Piece::Empty;
    if (moving == Piece::Empty)
        return;
    const bool moverWasRed = Piece::isRed(moving);
    halfmoveClock = (captured != Piece::Empty || Piece::typeOf(moving) == Piece::Pawn)
        ? 0 : halfmoveClock + 1;
    if (!moverWasRed)
        ++fullmoveNumber;
    redTurn = !moverWasRed;
}

bool Position::insidePalace(bool red, int row, int col) const
{
    if (col < 3 || col > 5)
        return false;
    return red ? (row >= 7 && row <= 9) : (row >= 0 && row <= 2);
}

void Position::addPseudoMoves(int from, QVector<Move> &moves) const
{
    const char p = piece(from);
    if (p == Piece::Empty)
        return;
    const bool red = Piece::isRed(p);
    const int row = rowOf(from);
    const int col = colOf(from);
    auto add = [&](int r, int c) {
        if (r < 0 || r >= 10 || c < 0 || c >= 9)
            return;
        const int to = square(r, c);
        if (!Piece::sameColor(p, piece(to)))
            moves.append({from, to});
    };

    switch (Piece::typeOf(p)) {
    case Piece::King: {
        const int dr[] = {-1, 1, 0, 0};
        const int dc[] = {0, 0, -1, 1};
        for (int i = 0; i < 4; ++i) {
            const int r = row + dr[i];
            const int c = col + dc[i];
            if (insidePalace(red, r, c))
                add(r, c);
        }
        // The flying-general capture also makes facing kings count as check.
        for (int step : {-1, 1}) {
            for (int r = row + step; r >= 0 && r < 10; r += step) {
                const char target = piece(r, col);
                if (target == Piece::Empty)
                    continue;
                if (Piece::typeOf(target) == Piece::King && !Piece::sameColor(p, target))
                    moves.append({from, square(r, col)});
                break;
            }
        }
        break;
    }
    case Piece::Advisor:
        for (int dr : {-1, 1})
            for (int dc : {-1, 1})
                if (insidePalace(red, row + dr, col + dc))
                    add(row + dr, col + dc);
        break;
    case Piece::Elephant:
        for (int dr : {-2, 2}) {
            for (int dc : {-2, 2}) {
                const int r = row + dr;
                const int c = col + dc;
                if (r < 0 || r >= 10 || c < 0 || c >= 9)
                    continue;
                if (red ? r < 5 : r > 4)
                    continue;
                if (piece(row + dr / 2, col + dc / 2) == Piece::Empty)
                    add(r, c);
            }
        }
        break;
    case Piece::Horse: {
        struct HorseStep { int dr, dc, legR, legC; };
        const HorseStep steps[] = {
            {-2, -1, -1, 0}, {-2, 1, -1, 0}, {2, -1, 1, 0}, {2, 1, 1, 0},
            {-1, -2, 0, -1}, {1, -2, 0, -1}, {-1, 2, 0, 1}, {1, 2, 0, 1}
        };
        for (const auto &step : steps) {
            if (piece(row + step.legR, col + step.legC) == Piece::Empty)
                add(row + step.dr, col + step.dc);
        }
        break;
    }
    case Piece::Rook:
    case Piece::Cannon: {
        const int dr[] = {-1, 1, 0, 0};
        const int dc[] = {0, 0, -1, 1};
        const bool cannon = Piece::typeOf(p) == Piece::Cannon;
        for (int direction = 0; direction < 4; ++direction) {
            bool screenFound = false;
            for (int r = row + dr[direction], c = col + dc[direction];
                 r >= 0 && r < 10 && c >= 0 && c < 9;
                 r += dr[direction], c += dc[direction]) {
                const char target = piece(r, c);
                if (!cannon) {
                    if (target == Piece::Empty)
                        moves.append({from, square(r, c)});
                    else {
                        if (!Piece::sameColor(p, target))
                            moves.append({from, square(r, c)});
                        break;
                    }
                } else if (!screenFound) {
                    if (target == Piece::Empty)
                        moves.append({from, square(r, c)});
                    else
                        screenFound = true;
                } else if (target != Piece::Empty) {
                    if (!Piece::sameColor(p, target))
                        moves.append({from, square(r, c)});
                    break;
                }
            }
        }
        break;
    }
    case Piece::Pawn: {
        const int forward = red ? -1 : 1;
        add(row + forward, col);
        const bool crossedRiver = red ? row <= 4 : row >= 5;
        if (crossedRiver) {
            add(row, col - 1);
            add(row, col + 1);
        }
        break;
    }
    default:
        break;
    }
}

QVector<Move> Position::pseudoMoves(bool red) const
{
    QVector<Move> moves;
    for (int i = 0; i < 90; ++i) {
        const char p = piece(i);
        if (p != Piece::Empty && Piece::isRed(p) == red)
            addPseudoMoves(i, moves);
    }
    return moves;
}

QVector<Move> Position::legalMoves() const
{
    const bool red = redTurn;
    QVector<Move> result;
    const QVector<Move> candidates = pseudoMoves(red);
    for (const Move &move : candidates) {
        Position next = *this;
        const char moving = next.piece(move.from);
        next.board[static_cast<size_t>(move.to)] = moving;
        next.board[static_cast<size_t>(move.from)] = Piece::Empty;
        if (!next.isInCheck(red))
            result.append(move);
    }
    return result;
}

QVector<Move> Position::legalMovesFrom(int squareIndex) const
{
    QVector<Move> result;
    const QVector<Move> all = legalMoves();
    for (const Move &move : all)
        if (move.from == squareIndex)
            result.append(move);
    return result;
}

bool Position::isLegalMove(const Move &move) const
{
    return legalMovesFrom(move.from).contains(move);
}

bool Position::isInCheck(bool red) const
{
    int kingSquare = -1;
    const char king = Piece::make(red, Piece::King);
    for (int i = 0; i < 90; ++i)
        if (piece(i) == king) {
            kingSquare = i;
            break;
        }
    if (kingSquare < 0)
        return true;
    const QVector<Move> attacks = pseudoMoves(!red);
    for (const Move &move : attacks)
        if (move.to == kingSquare)
            return true;
    return false;
}

bool Position::hasAnyLegalMove() const
{
    const QVector<Move> candidates = pseudoMoves(redTurn);
    for (const Move &move : candidates) {
        Position next = *this;
        next.board[static_cast<size_t>(move.to)] = next.piece(move.from);
        next.board[static_cast<size_t>(move.from)] = Piece::Empty;
        if (!next.isInCheck(redTurn))
            return true;
    }
    return false;
}

QString Position::validateForAnalysis() const
{
    int redKings = 0;
    int blackKings = 0;
    for (int index = 0; index < 90; ++index) {
        const char p = board[static_cast<size_t>(index)];
        const int row = rowOf(index);
        const int col = colOf(index);
        if (p == Piece::King)
            ++redKings;
        else if (p == Piece::make(false, Piece::King))
            ++blackKings;
        else if (p != Piece::Empty && QStringLiteral("KABNRCPkabnrcp").indexOf(QChar::fromLatin1(p)) < 0)
            return QStringLiteral("棋盘包含无法识别的棋子。");
        if (p == Piece::Empty)
            continue;
        const bool red = Piece::isRed(p);
        switch (Piece::typeOf(p)) {
        case Piece::King:
        case Piece::Advisor:
            if (!insidePalace(red, row, col))
                return QStringLiteral("%1必须位于己方九宫内。").arg(Piece::displayName(p));
            break;
        case Piece::Elephant:
            if (red ? row < 5 : row > 4)
                return QStringLiteral("相和象不能过河，请检查摆棋。");
            break;
        case Piece::Pawn:
            if (red ? row > 6 : row < 3)
                return QStringLiteral("兵卒位于不可能到达的起始区域，请检查摆棋。");
            break;
        default:
            break;
        }
    }
    if (redKings != 1 || blackKings != 1)
        return QStringLiteral("棋盘必须各有且仅有一个红帅和黑将。");
    if (isInCheck(true) && isInCheck(false))
        return QStringLiteral("将帅照面或双方同时被将军，请检查摆棋。");
    if (isInCheck(!redTurn))
        return QStringLiteral("%1方刚被将军，应由%1方走棋；请切换行棋方。")
            .arg(redTurn ? QStringLiteral("黑") : QStringLiteral("红"));
    return {};
}

QString Position::toFen() const
{
    QStringList ranks;
    for (int row = 0; row < 10; ++row) {
        QString rank;
        int blanks = 0;
        for (int col = 0; col < 9; ++col) {
            const char p = piece(row, col);
            if (p == Piece::Empty) {
                ++blanks;
            } else {
                if (blanks > 0)
                    rank += QString::number(blanks);
                blanks = 0;
                rank += QChar::fromLatin1(p);
            }
        }
        if (blanks > 0)
            rank += QString::number(blanks);
        ranks.append(rank);
    }
    return QStringLiteral("%1 %2 - - %3 %4")
        .arg(ranks.join(QLatin1Char('/')), redTurn ? QStringLiteral("w") : QStringLiteral("b"))
        .arg(halfmoveClock).arg(fullmoveNumber);
}

bool Position::fromFen(const QString &fen, Position &position, QString *error)
{
    Position parsed;
    parsed.clear();
    const QStringList fields = fen.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (fields.size() < 2) {
        if (error) *error = QStringLiteral("FEN 至少需要棋盘和行棋方两个字段。");
        return false;
    }
    const QStringList ranks = fields[0].split(QLatin1Char('/'));
    if (ranks.size() != 10) {
        if (error) *error = QStringLiteral("FEN 棋盘必须包含 10 行。");
        return false;
    }
    for (int row = 0; row < 10; ++row) {
        int col = 0;
        for (const QChar ch : ranks[row]) {
            if (ch.isDigit()) {
                const int count = ch.digitValue();
                if (count <= 0) {
                    if (error) *error = QStringLiteral("FEN 包含无效空位数。");
                    return false;
                }
                col += count;
            } else {
                const char p = ch.toLatin1();
                if (QStringLiteral("KABNRCPkabnrcp").indexOf(ch) < 0) {
                    if (error) *error = QStringLiteral("FEN 包含未知棋子：%1").arg(ch);
                    return false;
                }
                if (col >= 9) {
                    if (error) *error = QStringLiteral("FEN 第 %1 行超过 9 列。").arg(row + 1);
                    return false;
                }
                parsed.setPiece(square(row, col), p);
                ++col;
            }
            if (col > 9) {
                if (error) *error = QStringLiteral("FEN 第 %1 行超过 9 列。").arg(row + 1);
                return false;
            }
        }
        if (col != 9) {
            if (error) *error = QStringLiteral("FEN 第 %1 行不足 9 列。").arg(row + 1);
            return false;
        }
    }
    if (fields[1] != QStringLiteral("w") && fields[1] != QStringLiteral("b")) {
        if (error) *error = QStringLiteral("FEN 行棋方只能是 w（红）或 b（黑）。");
        return false;
    }
    parsed.redTurn = fields[1] == QStringLiteral("w");
    if (fields.size() >= 6) {
        bool halfOk = false;
        bool fullOk = false;
        const int half = fields[4].toInt(&halfOk);
        const int full = fields[5].toInt(&fullOk);
        if (halfOk && half >= 0)
            parsed.halfmoveClock = half;
        if (fullOk && full > 0)
            parsed.fullmoveNumber = full;
    }
    position = parsed;
    return true;
}

QString Position::moveToUci(const Move &move)
{
    if (!move.isValid())
        return {};
    auto coordinate = [](int squareIndex) {
        return QStringLiteral("%1%2")
            .arg(QChar::fromLatin1(static_cast<char>('a' + colOf(squareIndex))))
            .arg(9 - rowOf(squareIndex));
    };
    return coordinate(move.from) + coordinate(move.to);
}

Move Position::moveFromUci(const QString &text)
{
    const QString move = text.trimmed();
    if (move.size() < 4)
        return {};
    auto squareFrom = [](QChar file, QChar rank) {
        if (file < QLatin1Char('a') || file > QLatin1Char('i')
            || rank < QLatin1Char('0') || rank > QLatin1Char('9'))
            return -1;
        const int col = file.toLatin1() - 'a';
        const int row = 9 - (rank.toLatin1() - '0');
        return square(row, col);
    };
    return {squareFrom(move[0], move[1]), squareFrom(move[2], move[3])};
}

QString Position::moveNotation(const Move &move) const
{
    if (!move.isValid())
        return {};
    const char p = piece(move.from);
    if (p == Piece::Empty)
        return moveToUci(move);
    const bool red = Piece::isRed(p);
    const int fromRow = rowOf(move.from);
    const int fromCol = colOf(move.from);
    const int toRow = rowOf(move.to);
    const int toCol = colOf(move.to);
    const QString redDigits = QStringLiteral("一二三四五六七八九");
    auto fileName = [&](int col) {
        return red ? QString(redDigits[8 - col]) : QString::number(9 - col);
    };
    auto amountName = [&](int amount) {
        return red ? QString(redDigits[amount - 1]) : QString::number(amount);
    };

    QVector<int> companions;
    for (int row = 0; row < 10; ++row) {
        const int index = square(row, fromCol);
        if (piece(index) == p)
            companions.append(index);
    }
    QString origin;
    if (companions.size() >= 2) {
        std::sort(companions.begin(), companions.end(), [red](int a, int b) {
            return red ? rowOf(a) < rowOf(b) : rowOf(a) > rowOf(b);
        });
        const int index = companions.indexOf(move.from);
        if (companions.size() == 2)
            origin = index == 0 ? QStringLiteral("前") : QStringLiteral("后");
        else if (companions.size() == 3)
            origin = index == 0 ? QStringLiteral("前") : (index == 1 ? QStringLiteral("中") : QStringLiteral("后"));
        else
            origin = QStringLiteral("前");
        origin += Piece::displayName(p);
    } else {
        origin = Piece::displayName(p) + fileName(fromCol);
    }

    if (fromRow == toRow)
        return origin + QStringLiteral("平") + fileName(toCol);
    const bool forward = red ? toRow < fromRow : toRow > fromRow;
    const QString direction = forward ? QStringLiteral("进") : QStringLiteral("退");
    const char type = Piece::typeOf(p);
    if (fromCol == toCol && type != Piece::Horse && type != Piece::Elephant && type != Piece::Advisor)
        return origin + direction + amountName(std::abs(toRow - fromRow));
    return origin + direction + fileName(toCol);
}
