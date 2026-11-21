#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

struct MacWindowInfo {
    quint32 id = 0;
    int processId = 0;
    QString applicationName;
    QString title;
    QRectF bounds;
    QImage thumbnail;

    QString displayName() const
    {
        if (title.isEmpty() || title == applicationName)
            return applicationName;
        return applicationName + QStringLiteral(" · ") + title;
    }
};

namespace MacAutomation {
bool hasScreenCaptureAccess();
bool requestScreenCaptureAccess();
bool hasAccessibilityAccess();
bool requestAccessibilityAccess();
QVector<MacWindowInfo> availableWindows(QString *error = nullptr);
QRectF windowBounds(quint32 windowId);
bool activateWindow(const MacWindowInfo &window, QString *error);
QImage captureWindowImage(quint32 windowId, const QSize &maximumSize = {}, QString *error = nullptr);
bool captureWindow(quint32 windowId, const QString &fileName, QString *error);
void click(const QPointF &point);
}
