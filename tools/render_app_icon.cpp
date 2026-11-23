#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QString>

#include <algorithm>

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    if (argc != 2) {
        qInfo("Usage: render_app_icon OUTPUT.png");
        return 2;
    }

    constexpr int size = 1024;
    QImage image(size, size, QImage::Format_RGB888);
    image.fill(QStringLiteral("#173a34"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath silhouette;
    silhouette.addRoundedRect(30, 30, 964, 964, 205, 205);
    painter.setClipPath(silhouette);

    QLinearGradient background(120, 40, 900, 1000);
    background.setColorAt(0.0, QStringLiteral("#315f50"));
    background.setColorAt(0.52, QStringLiteral("#204a41"));
    background.setColorAt(1.0, QStringLiteral("#102e2b"));
    painter.fillRect(image.rect(), background);

    // Soft pools of light give the board depth without adding visual clutter.
    QRadialGradient glow(QPointF(300, 190), 720);
    glow.setColorAt(0.0, QColor(235, 211, 150, 58));
    glow.setColorAt(1.0, QColor(235, 211, 150, 0));
    painter.fillRect(image.rect(), glow);

    // A restrained Xiangqi grid remains visible around the central piece.
    painter.setPen(QPen(QColor(218, 190, 125, 60), 7, Qt::SolidLine, Qt::RoundCap));
    for (int index = 0; index < 9; ++index) {
        const int x = 116 + index * 99;
        painter.drawLine(x, 120, x, 904);
    }
    for (int index = 0; index < 10; ++index) {
        const int y = 120 + index * 87;
        painter.drawLine(116, y, 908, y);
    }
    painter.setPen(QPen(QColor(233, 204, 137, 88), 10));
    painter.drawRoundedRect(99, 103, 826, 818, 25, 25);
    painter.drawLine(314, 120, 710, 381);
    painter.drawLine(710, 120, 314, 381);
    painter.drawLine(314, 643, 710, 904);
    painter.drawLine(710, 643, 314, 904);

    // Grounded shadow beneath the wooden chess piece.
    QRadialGradient shadow(QPointF(512, 824), 350);
    shadow.setColorAt(0.0, QColor(0, 0, 0, 145));
    shadow.setColorAt(0.55, QColor(0, 0, 0, 65));
    shadow.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillRect(QRect(130, 650, 764, 270), shadow);

    const QPointF center(512, 515);
    constexpr double radius = 326;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(5, 19, 16, 125));
    painter.drawEllipse(center + QPointF(0, 24), radius + 9, radius + 9);

    QRadialGradient rim(center - QPointF(95, 120), 570);
    rim.setColorAt(0.0, QStringLiteral("#fff3cd"));
    rim.setColorAt(0.58, QStringLiteral("#e8c887"));
    rim.setColorAt(0.86, QStringLiteral("#c49149"));
    rim.setColorAt(1.0, QStringLiteral("#8c592b"));
    painter.setBrush(rim);
    painter.setPen(QPen(QColor(QStringLiteral("#6f4325")), 9));
    painter.drawEllipse(center, radius, radius);

    QRadialGradient face(center - QPointF(100, 125), 520);
    face.setColorAt(0.0, QStringLiteral("#fffdf0"));
    face.setColorAt(0.68, QStringLiteral("#f7e7bd"));
    face.setColorAt(1.0, QStringLiteral("#dfbd78"));
    painter.setBrush(face);
    painter.setPen(QPen(QColor(QStringLiteral("#b7372e")), 15));
    painter.drawEllipse(center, 276, 276);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(183, 55, 46, 165), 5));
    painter.drawEllipse(center, 250, 250);

    // A subtle upper-left reflection makes the piece feel rounded.
    QPainterPath highlight;
    highlight.addEllipse(center - QPointF(0, 6), 296, 296);
    painter.setClipPath(highlight, Qt::IntersectClip);
    QLinearGradient sheen(QPointF(300, 230), QPointF(570, 590));
    sheen.setColorAt(0.0, QColor(255, 255, 255, 100));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(QRect(200, 190, 630, 450), sheen);
    painter.setClipping(false);

    QFont glyphFont(QStringLiteral("Kaiti SC"));
    glyphFont.setBold(true);
    glyphFont.setPixelSize(380);
    painter.setFont(glyphFont);
    painter.setPen(QStringLiteral("#b72f29"));
    painter.drawText(QRectF(212, 225, 600, 560), Qt::AlignCenter, QStringLiteral("帅"));

    // Fine inner border for a polished macOS icon silhouette.
    painter.setClipPath(silhouette);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(244, 222, 170, 75), 5));
    painter.drawRoundedRect(38, 38, 948, 948, 197, 197);
    painter.end();

    const QString output = QString::fromLocal8Bit(argv[1]);
    if (!image.save(output, "PNG")) {
        qWarning("Unable to save icon to %s", qPrintable(output));
        return 1;
    }
    return 0;
}
