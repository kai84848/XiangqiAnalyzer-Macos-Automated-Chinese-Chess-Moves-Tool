#include "macautomation.h"

#include <QImage>
#include <QThread>

#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ScreenCaptureKit/ScreenCaptureKit.h>

#include <algorithm>
#include <cmath>
#include <unistd.h>

namespace {
QString cfString(CFStringRef value)
{
    if (!value)
        return {};
    const CFIndex length = CFStringGetLength(value);
    const CFIndex maximum = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    QByteArray buffer(static_cast<int>(maximum), '\0');
    if (!CFStringGetCString(value, buffer.data(), maximum, kCFStringEncodingUTF8))
        return {};
    return QString::fromUtf8(buffer.constData());
}

void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

SCShareableContent *shareableContent(BOOL onScreenOnly, QString *error) API_AVAILABLE(macos(14.0))
{
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block SCShareableContent *content = nil;
    __block NSError *contentError = nil;
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:onScreenOnly
                                                 completionHandler:^(SCShareableContent *result, NSError *resultError) {
        content = [result retain];
        contentError = [resultError retain];
        dispatch_semaphore_signal(semaphore);
    }];
    const long completed = dispatch_semaphore_wait(semaphore,
        dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
    dispatch_release(semaphore);
    if (completed != 0) {
        if (content)
            [content release];
        if (contentError)
            [contentError release];
        setError(error, QStringLiteral("读取系统窗口列表超时"));
        return nil;
    }
    if (!content || contentError) {
        const QString message = contentError
            ? QString::fromNSString(contentError.localizedDescription)
            : QStringLiteral("没有读取到可共享的窗口");
        setError(error, QStringLiteral("无法读取窗口：%1").arg(message));
        if (content)
            [content release];
        if (contentError)
            [contentError release];
        return nil;
    }
    if (contentError)
        [contentError release];
    return content;
}

SCWindow *selectedNativeWindow(quint32 windowId, QString *error) API_AVAILABLE(macos(14.0))
{
    static quint32 cachedId = 0;
    static SCWindow *cachedWindow = nil;
    if (cachedWindow && cachedId == windowId)
        return cachedWindow;

    SCShareableContent *content = shareableContent(NO, error);
    if (!content)
        return nil;
    for (SCWindow *window in content.windows) {
        if (window.windowID != windowId)
            continue;
        [cachedWindow release];
        cachedWindow = [window retain];
        cachedId = windowId;
        [content release];
        return cachedWindow;
    }
    [content release];
    [cachedWindow release];
    cachedWindow = nil;
    cachedId = 0;
    setError(error, QStringLiteral("所选窗口已经关闭，请重新选择对弈窗口"));
    return nil;
}

QImage imageFromCGImage(CGImageRef image)
{
    if (!image)
        return {};
    NSBitmapImageRep *representation = [[NSBitmapImageRep alloc] initWithCGImage:image];
    NSData *png = [representation representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    const QImage decoded = png
        ? QImage::fromData(static_cast<const uchar *>(png.bytes), static_cast<int>(png.length))
        : QImage();
    [representation release];
    return decoded;
}

QImage captureNativeWindow(SCWindow *window, const QSize &maximumSize, QString *error) API_AVAILABLE(macos(14.0))
{
    if (!window || window.frame.size.width < 1 || window.frame.size.height < 1) {
        setError(error, QStringLiteral("所选窗口尺寸无效"));
        return {};
    }
    const double width = window.frame.size.width;
    const double height = window.frame.size.height;
    double scale = 2.0;
    if (maximumSize.isValid() && maximumSize.width() > 0 && maximumSize.height() > 0) {
        scale = std::min(maximumSize.width() / width, maximumSize.height() / height);
        scale = std::min(scale, 2.0);
    }

    SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
    SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
    configuration.width = static_cast<size_t>(std::max(1.0, std::round(width * scale)));
    configuration.height = static_cast<size_t>(std::max(1.0, std::round(height * scale)));
    configuration.showsCursor = NO;

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block CGImageRef capturedImage = nullptr;
    __block NSError *captureError = nil;
    [SCScreenshotManager captureImageWithFilter:filter
                                  configuration:configuration
                              completionHandler:^(CGImageRef image, NSError *resultError) {
        if (image)
            capturedImage = CGImageRetain(image);
        captureError = [resultError retain];
        dispatch_semaphore_signal(semaphore);
    }];
    const long completed = dispatch_semaphore_wait(semaphore,
        dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
    dispatch_release(semaphore);
    [filter release];
    [configuration release];
    if (completed != 0) {
        if (capturedImage)
            CGImageRelease(capturedImage);
        if (captureError)
            [captureError release];
        setError(error, QStringLiteral("截取所选窗口超时"));
        return {};
    }

    const QImage result = imageFromCGImage(capturedImage);
    if (capturedImage)
        CGImageRelease(capturedImage);
    if (result.isNull()) {
        const QString message = captureError
            ? QString::fromNSString(captureError.localizedDescription)
            : QStringLiteral("系统没有返回窗口图像");
        setError(error, QStringLiteral("截取窗口失败：%1").arg(message));
    }
    if (captureError)
        [captureError release];
    return result;
}
}

namespace MacAutomation {
bool hasScreenCaptureAccess()
{
    return CGPreflightScreenCaptureAccess();
}

bool requestScreenCaptureAccess()
{
    return CGRequestScreenCaptureAccess();
}

bool hasAccessibilityAccess()
{
    return AXIsProcessTrusted();
}

bool requestAccessibilityAccess()
{
    const void *keys[] = {kAXTrustedCheckOptionPrompt};
    const void *values[] = {kCFBooleanTrue};
    CFDictionaryRef options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const bool trusted = AXIsProcessTrustedWithOptions(options);
    if (options)
        CFRelease(options);
    return trusted;
}

QVector<MacWindowInfo> availableWindows(QString *error)
{
    QVector<MacWindowInfo> result;
    if (@available(macOS 14.0, *)) {
        SCShareableContent *content = shareableContent(YES, error);
        if (!content)
            return result;
        for (SCWindow *window in content.windows) {
            SCRunningApplication *application = window.owningApplication;
            if (!application || application.processID == getpid()
                || window.windowLayer != 0
                || window.frame.size.width < 320 || window.frame.size.height < 240)
                continue;
            MacWindowInfo info;
            info.id = window.windowID;
            info.processId = application.processID;
            info.applicationName = QString::fromNSString(application.applicationName);
            info.title = QString::fromNSString(window.title);
            info.bounds = QRectF(window.frame.origin.x, window.frame.origin.y,
                                 window.frame.size.width, window.frame.size.height);
            info.thumbnail = captureNativeWindow(window, QSize(480, 300), nullptr);
            result.append(info);
        }
        [content release];
    } else {
        setError(error, QStringLiteral("窗口选择需要 macOS 14 或更高版本"));
    }
    return result;
}

QRectF windowBounds(quint32 windowId)
{
    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!windows)
        return {};
    QRectF result;
    const CFIndex count = CFArrayGetCount(windows);
    for (CFIndex index = 0; index < count; ++index) {
        const auto *info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, index));
        const auto *numberValue = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowNumber));
        int number = 0;
        if (!numberValue || !CFNumberGetValue(numberValue, kCFNumberIntType, &number)
            || static_cast<quint32>(number) != windowId)
            continue;
        const auto *boundsValue = static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds));
        CGRect bounds = CGRectZero;
        if (boundsValue && CGRectMakeWithDictionaryRepresentation(boundsValue, &bounds))
            result = QRectF(bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);
        break;
    }
    CFRelease(windows);
    return result;
}

bool activateWindow(const MacWindowInfo &window, QString *error)
{
    NSRunningApplication *application =
        [NSRunningApplication runningApplicationWithProcessIdentifier:window.processId];
    if (!application) {
        setError(error, QStringLiteral("所选窗口的应用已经退出，请重新选择"));
        return false;
    }
    const BOOL activated = [application activateWithOptions:0];
    if (!activated) {
        setError(error, QStringLiteral("无法切换到所选的对弈窗口"));
        return false;
    }

    AXUIElementRef axApplication = AXUIElementCreateApplication(window.processId);
    CFArrayRef axWindows = nullptr;
    if (axApplication
        && AXUIElementCopyAttributeValue(axApplication, kAXWindowsAttribute,
                                         reinterpret_cast<CFTypeRef *>(&axWindows)) == kAXErrorSuccess
        && axWindows) {
        const CFIndex count = CFArrayGetCount(axWindows);
        for (CFIndex index = 0; index < count; ++index) {
            auto axWindow = static_cast<AXUIElementRef>(const_cast<void *>(
                CFArrayGetValueAtIndex(axWindows, index)));
            CFStringRef title = nullptr;
            if (AXUIElementCopyAttributeValue(axWindow, kAXTitleAttribute,
                                              reinterpret_cast<CFTypeRef *>(&title)) != kAXErrorSuccess)
                continue;
            const QString axTitle = cfString(title);
            if (title)
                CFRelease(title);
            const bool titleMatches = !window.title.isEmpty() && axTitle == window.title;
            if (!titleMatches)
                continue;
            AXUIElementSetAttributeValue(axWindow, kAXMinimizedAttribute, kCFBooleanFalse);
            AXUIElementPerformAction(axWindow, kAXRaiseAction);
            AXUIElementSetAttributeValue(axApplication, kAXMainWindowAttribute, axWindow);
            break;
        }
    }
    if (axWindows)
        CFRelease(axWindows);
    if (axApplication)
        CFRelease(axApplication);
    return true;
}

QImage captureWindowImage(quint32 windowId, const QSize &maximumSize, QString *error)
{
    if (@available(macOS 14.0, *)) {
        SCWindow *window = selectedNativeWindow(windowId, error);
        return window ? captureNativeWindow(window, maximumSize, error) : QImage();
    }
    setError(error, QStringLiteral("窗口截图需要 macOS 14 或更高版本"));
    return {};
}

bool captureWindow(quint32 windowId, const QString &fileName, QString *error)
{
    const QImage screenshot = captureWindowImage(windowId, {}, error);
    if (screenshot.isNull())
        return false;
    if (!screenshot.save(fileName, "PNG")) {
        setError(error, QStringLiteral("无法保存自动对弈截图"));
        return false;
    }
    return true;
}

void click(const QPointF &point)
{
    const CGPoint location = CGPointMake(point.x(), point.y());
    CGEventRef move = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, location, kCGMouseButtonLeft);
    CGEventRef down = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseDown, location, kCGMouseButtonLeft);
    CGEventRef up = CGEventCreateMouseEvent(nullptr, kCGEventLeftMouseUp, location, kCGMouseButtonLeft);
    if (move)
        CGEventPost(kCGHIDEventTap, move);
    QThread::msleep(70);
    if (down)
        CGEventPost(kCGHIDEventTap, down);
    QThread::msleep(60);
    if (up)
        CGEventPost(kCGHIDEventTap, up);
    if (move)
        CFRelease(move);
    if (down)
        CFRelease(down);
    if (up)
        CFRelease(up);
}
}
