#include "bgpatch.h"

#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QApplication>
#include <QAbstractScrollArea>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QScrollBar>
#include <QMainWindow>

#include <windows.h>

// ─── OverlayWidget ───────────────────────────────────────────────────

OverlayWidget::OverlayWidget(bool useNative, QWidget *parent)
    : QWidget(parent), m_native(useNative)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    // Native window only needed for editor mode (competes with QScintilla HWND).
    // Window mode uses plain Qt widgets — native windows there block mouse input.
    if (m_native)
        setAttribute(Qt::WA_NativeWindow);
    raise();
}

bool OverlayWidget::event(QEvent *e)
{
    // For non-native overlays (Scope=2), explicitly reject all input events
    // so Qt propagates them to the widgets underneath.  WA_TransparentForMouseEvents
    // handles hit testing, but an explicit ignore covers edge cases with
    // QMainWindow's managed children (dock widgets, menu bar, etc.).
    if (!m_native) {
        switch (e->type()) {
        case QEvent::MouseButtonPress:   case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick: case QEvent::MouseMove:
        case QEvent::Wheel:              case QEvent::KeyPress:
        case QEvent::KeyRelease:         case QEvent::HoverEnter:
        case QEvent::HoverLeave:         case QEvent::HoverMove:
        case QEvent::Enter:              case QEvent::Leave:
        case QEvent::FocusIn:            case QEvent::FocusOut:
        case QEvent::ContextMenu:        case QEvent::DragEnter:
        case QEvent::DragLeave:          case QEvent::DragMove:
        case QEvent::Drop:
            e->ignore();
            return false;
        default: break;
        }
    }
    return QWidget::event(e);
}

void OverlayWidget::setImage(const QImage &img, int opacity, int dimming, int scaleMode)
{
    m_image     = img;
    m_opacity   = qBound(0, opacity,  100);
    m_dimming   = qBound(0, dimming,  100);
    m_scaleMode = qBound(0, scaleMode, 4);
    update();
}

void OverlayWidget::paintEvent(QPaintEvent *)
{
    if (m_image.isNull()) return;

    QPainter painter(this);
    QSize sz = size();
    if (sz.isEmpty()) return;

    QImage scaled;
    switch (m_scaleMode) {
    case 0: // Fit
        scaled = m_image.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        break;
    case 1: // Fill (default)
        scaled = m_image.scaled(sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        break;
    case 2: // Stretch
        scaled = m_image.scaled(sz, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        break;
    case 3: // Center
        scaled = QImage(sz, QImage::Format_ARGB32);
        scaled.fill(Qt::transparent);
        {
            QPainter sp(&scaled);
            int ox = (sz.width()  - m_image.width())  / 2;
            int oy = (sz.height() - m_image.height()) / 2;
            sp.drawImage(ox, oy, m_image);
        }
        break;
    case 4: // Tile
        scaled = QImage(sz, QImage::Format_ARGB32);
        scaled.fill(Qt::transparent);
        {
            QPainter sp(&scaled);
            for (int y = 0; y < sz.height(); y += m_image.height())
                for (int x = 0; x < sz.width();  x += m_image.width())
                    sp.drawImage(x, y, m_image);
        }
        break;
    default:
        scaled = m_image.scaled(sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        break;
    }

    painter.setOpacity(m_opacity / 100.0);

    int ox = (sz.width()  - scaled.width())  / 2;
    int oy = (sz.height() - scaled.height()) / 2;
    painter.drawImage(ox, oy, scaled);

    painter.setOpacity(1.0);
    if (m_dimming > 0) {
        painter.fillRect(rect(), QColor(0, 0, 0, m_dimming * 255 / 100));
    }
}

// ─── BackgroundImageEffect ───────────────────────────────────────────

BackgroundImageEffect::BackgroundImageEffect(Scope scope, QObject *parent)
    : QObject(parent), m_scope(scope) {}

BackgroundImageEffect::~BackgroundImageEffect()
{
    detach();
    if (m_editor) {
        m_editor->removeEventFilter(this);
        if (m_scope == ScopeEditor) {
            auto *area = static_cast<QAbstractScrollArea*>(m_editor.data());
            if (area && area->viewport())
                area->viewport()->removeEventFilter(this);
        }
    }
}

void BackgroundImageEffect::attachToEditor(QWidget *editorWidget)
{
    if (m_editor == editorWidget) return;
    detach();
    m_editor = editorWidget;
    if (!m_editor) return;

    auto *area = static_cast<QAbstractScrollArea*>(m_editor.data());
    QWidget *vp = area ? area->viewport() : nullptr;
    if (!vp) {
        logWrite("WARN: No viewport found for editor %p", (void*)m_editor.data());
        return;
    }

    QSize vs = area->maximumViewportSize();
    if (vs.isEmpty()) vs = vp->size();

    m_overlay = new OverlayWidget(true, vp);  // native HWND to beat QScintilla
    m_overlay->resize(vs);
    m_overlay->move(0, 0);
    m_overlay->show();
    m_overlay->raise();
    if (m_overlay->internalWinId())
        SetWindowPos(reinterpret_cast<HWND>(m_overlay->winId()),
                     HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    logWrite("Overlay created (editor): size=(%d,%d) vpSize=(%d,%d) imageNull=%d opacity=%d",
             vs.width(), vs.height(),
             vp->size().width(), vp->size().height(),
             (int)m_image.isNull(), m_opacity);

    updateOverlay();

    m_editor->installEventFilter(this);
    vp->installEventFilter(this);

    if (area->verticalScrollBar()) {
        QObject::connect(area->verticalScrollBar(), &QScrollBar::valueChanged,
                         [this](int) { repositionOverlay(); });
    }
    if (area->horizontalScrollBar()) {
        QObject::connect(area->horizontalScrollBar(), &QScrollBar::valueChanged,
                         [this](int) { repositionOverlay(); });
    }
}

void BackgroundImageEffect::attachToWindow(QWidget *mainWindow)
{
    if (m_editor == mainWindow) return;
    detach();
    m_editor = mainWindow;
    if (!m_editor) return;

    // CREATE A SEPARATE TOP-LEVEL WINDOW (parent = nullptr).
    // This avoids QMainWindow's custom event dispatch which would otherwise
    // intercept mouse events before they could pass through the overlay.
    m_overlay = new OverlayWidget(false, nullptr);

    // Frameless tool window: no taskbar entry
    m_overlay->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    m_overlay->setAttribute(Qt::WA_TranslucentBackground);
    m_overlay->setAttribute(Qt::WA_ShowWithoutActivating);
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_overlay->setAttribute(Qt::WA_NativeWindow);  // needed for WS_EX_... below

    // Win32: layered (per-pixel alpha) + transparent (mouse passthrough)
    HWND hwnd = reinterpret_cast<HWND>(m_overlay->winId());
    LONG ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                      ex | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);

    // Position over the main window in screen coordinates
    repositionOverlay();

    // Bind the overlay as an Owned window of the Octave main window at the
    // Win32 level.  This keeps the overlay always on top of Octave, but lets
    // other applications cover both when they are moved over Octave.
    HWND hwndOverlay = reinterpret_cast<HWND>(m_overlay->winId());
    HWND hwndMain = reinterpret_cast<HWND>(m_editor->window()->winId());
    SetWindowLongPtrW(hwndOverlay, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(hwndMain));

    m_overlay->show();

    logWrite("Overlay created (window-top-level): pos=(%d,%d) size=(%d,%d) imageNull=%d opacity=%d",
             m_overlay->x(), m_overlay->y(),
             m_overlay->width(), m_overlay->height(),
             (int)m_image.isNull(), m_opacity);

    updateOverlay();

    // Track the main window when it moves or resizes
    m_editor->installEventFilter(this);
}

void BackgroundImageEffect::detach()
{
    if (m_overlay) {
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
    if (m_editor) {
        m_editor->removeEventFilter(this);
        if (m_scope == ScopeEditor) {
            auto *area = static_cast<QAbstractScrollArea*>(m_editor.data());
            if (area && area->viewport())
                area->viewport()->removeEventFilter(this);
        }
        m_editor = nullptr;
    }
}

bool BackgroundImageEffect::isAlive() const
{
    return !m_editor.isNull();
}

bool BackgroundImageEffect::eventFilter(QObject *obj, QEvent *event)
{
    if (!m_editor || !m_overlay)
        return false;

    if (event->type() == QEvent::Destroy) {
        if (obj == m_editor.data()) {
            m_editor = nullptr;
            if (m_overlay) {
                m_overlay->deleteLater();
                m_overlay = nullptr;
            }
        }
        return false;
    }

    if (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
        event->type() == QEvent::Show) {
        repositionOverlay();
    }
    return false;
}

void BackgroundImageEffect::repositionOverlay()
{
    if (!m_overlay || !m_editor) return;

    if (m_scope == ScopeWindow) {
        // Position the independent top-level overlay over the main window
        QPoint topLeft = m_editor->mapToGlobal(QPoint(0, 0));
        QSize sz = m_editor->size();
        m_overlay->setGeometry(topLeft.x(), topLeft.y(), sz.width(), sz.height());
        m_overlay->raise();

        // The owner relationship keeps the overlay on top of the main window.
        // No need for HWND_TOPMOST — SWP_NOZORDER lets the system handle it.
        if (m_overlay->internalWinId())
            SetWindowPos(reinterpret_cast<HWND>(m_overlay->winId()),
                         nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        return;
    }

    // Editor mode: scroll-aware positioning within the viewport
    auto *area = static_cast<QAbstractScrollArea*>(m_editor.data());
    if (!area || !area->viewport()) return;

    int sx = area->horizontalScrollBar() ? area->horizontalScrollBar()->value() : 0;
    int sy = area->verticalScrollBar()   ? area->verticalScrollBar()->value()   : 0;

    QSize vs = area->maximumViewportSize();
    if (vs.isEmpty()) vs = area->viewport()->size();

    m_overlay->resize(vs);
    m_overlay->move(sx, sy);
    m_overlay->raise();

    if (m_overlay->internalWinId())
        SetWindowPos(reinterpret_cast<HWND>(m_overlay->winId()),
                     HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void BackgroundImageEffect::updateOverlay()
{
    if (m_overlay) {
        m_overlay->setImage(m_image, m_opacity, m_dimming, m_scaleMode);
        m_overlay->raise();
        if (m_overlay->internalWinId())
            SetWindowPos(reinterpret_cast<HWND>(m_overlay->winId()),
                         HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void BackgroundImageEffect::reloadImage()
{
    if (m_imagePath.isEmpty()) {
        m_image = QImage();
    } else {
        m_image = QImage(m_imagePath);
        if (m_image.isNull()) {
            QByteArray p = m_imagePath.toUtf8();
            logWrite("Failed to load image: %s", p.constData());
        }
    }
    updateOverlay();
}

void BackgroundImageEffect::setImage(const QString &path)
{
    m_imagePath = path;
    reloadImage();
}

void BackgroundImageEffect::setOpacity(int percent)
{
    m_opacity = qBound(0, percent, 100);
    updateOverlay();
}

void BackgroundImageEffect::setDimming(int percent)
{
    m_dimming = qBound(0, percent, 100);
    updateOverlay();
}

void BackgroundImageEffect::setScaleMode(int mode)
{
    m_scaleMode = qBound(0, mode, 4);
    updateOverlay();
}

// ─── BackgroundManager (singleton) ───────────────────────────────────

BackgroundManager *BackgroundManager::instance()
{
    static BackgroundManager *inst = nullptr;
    if (!inst) inst = new BackgroundManager();
    return inst;
}

BackgroundManager::BackgroundManager(QObject *parent)
    : QObject(parent)
{
    m_scanTimer = new QTimer(this);
    m_scanTimer->setInterval(2000);
    QObject::connect(m_scanTimer, &QTimer::timeout, [this]() { scanAndAttach(); });
}

void BackgroundManager::init()
{
    reloadSettings();
    QTimer::singleShot(1000, [this]() { scanAndAttach(); });
    m_scanTimer->start();
}

void BackgroundManager::reloadSettings()
{
    wchar_t dllPath[MAX_PATH];
    HMODULE hMod;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&BackgroundManager::instance, &hMod);
    GetModuleFileNameW(hMod, dllPath, MAX_PATH);
    QString dllDir = QFileInfo(QString::fromWCharArray(dllPath)).absolutePath();
    QString iniPath = dllDir + "/bgpatch.ini";

    QSettings settings(iniPath, QSettings::IniFormat);
    QString imagePath = settings.value("Background/Image", "").toString();
    imagePath.replace(QLatin1Char('/'), QLatin1Char('\\'));
    int opacity   = settings.value("Background/Opacity",   30).toInt();
    int dimming   = settings.value("Background/Dimming",   30).toInt();
    int scaleMode = settings.value("Background/ScaleMode", 1).toInt();
    int scopeVal  = settings.value("Background/Scope",     1).toInt();
    int debugLog  = settings.value("Background/DebugLog",  0).toInt();
    logSetEnabled(debugLog != 0);

    Scope newScope = (scopeVal == 2) ? ScopeWindow : ScopeEditor;

    bool scopeChanged = (newScope != m_currentScope);

    for (auto *fx : m_effects) {
        fx->setImage(imagePath);
        fx->setOpacity(opacity);
        fx->setDimming(dimming);
        fx->setScaleMode(scaleMode);
    }

    m_pendingImage     = imagePath;
    m_pendingOpacity   = opacity;
    m_pendingDimming   = dimming;
    m_pendingScaleMode = scaleMode;

    if (scopeChanged) {
        logWrite("Scope changed: %d -> %d, re-attaching", (int)m_currentScope, (int)newScope);
        m_currentScope = newScope;
        // Clear everything and re-scan
        for (auto *fx : m_effects)
            delete fx;
        m_effects.clear();
        m_attached.clear();
        scanAndAttach();
    }
}

void BackgroundManager::scanAndAttach()
{
    // Clean up dead effects
    for (int i = m_effects.size() - 1; i >= 0; i--) {
        if (!m_effects[i]->isAlive()) {
            delete m_effects[i];
            m_effects.removeAt(i);
            m_attached.removeAt(i);
        }
    }

    if (m_currentScope == ScopeWindow) {
        attachWindow();
    } else {
        attachEditors();
    }
}

void BackgroundManager::attachEditors()
{
    static bool firstScan = true;

    QWidgetList all = QApplication::allWidgets();
    for (QWidget *w : all) {
        if (m_attached.contains(w)) continue;

        const QMetaObject *mo = w->metaObject();
        bool isScintilla = false;
        QString matchedName;
        while (mo) {
            QString cn = QString::fromLatin1(mo->className());
            if (cn == QLatin1String("QsciScintilla") ||
                cn == QLatin1String("QsciScintillaBase") ||
                cn.contains(QLatin1String("Scintilla"))) {
                isScintilla = true;
                matchedName = cn;
                break;
            }
            mo = mo->superClass();
        }

        if (firstScan && w->isWidgetType() && w->isVisible()) {
            const QMetaObject *dmo = w->metaObject();
            QString chain;
            while (dmo) {
                if (!chain.isEmpty()) chain += " -> ";
                chain += QString::fromLatin1(dmo->className());
                dmo = dmo->superClass();
            }
            QByteArray utf8 = chain.toUtf8();
            logWrite("  widget %p: %s", (void*)w, utf8.constData());
        }

        if (isScintilla) {
            QByteArray mn = matchedName.toUtf8();
            logWrite("FOUND editor: %p  class=%s  size=(%d,%d)  visible=%d",
                     (void*)w, mn.constData(),
                     w->size().width(), w->size().height(), (int)w->isVisible());

            auto *fx = new BackgroundImageEffect(ScopeEditor, this);
            fx->setImage(m_pendingImage);
            fx->setOpacity(m_pendingOpacity);
            fx->setDimming(m_pendingDimming);
            fx->setScaleMode(m_pendingScaleMode);
            fx->attachToEditor(w);

            m_effects.append(fx);
            m_attached.append(w);

            QByteArray img = m_pendingImage.toUtf8();
            logWrite("Attached (editor). image=%s opacity=%d",
                     img.isEmpty() ? "(empty)" : img.constData(), m_pendingOpacity);
        }
    }
    firstScan = false;
}

void BackgroundManager::attachWindow()
{
    // Find the main window — there should be exactly one
    QWidgetList all = QApplication::allWidgets();
    for (QWidget *w : all) {
        const QMetaObject *mo = w->metaObject();
        while (mo) {
            QString cn = QString::fromLatin1(mo->className());
            if (cn == QLatin1String("octave::main_window")) {
                if (!m_attached.contains(w)) {
                    logWrite("FOUND main window: %p  size=(%d,%d)",
                             (void*)w, w->size().width(), w->size().height());

                    auto *fx = new BackgroundImageEffect(ScopeWindow, this);
                    fx->setImage(m_pendingImage);
                    fx->setOpacity(m_pendingOpacity);
                    fx->setDimming(m_pendingDimming);
                    fx->setScaleMode(m_pendingScaleMode);
                    fx->attachToWindow(w);

                    m_effects.append(fx);
                    m_attached.append(w);

                    QByteArray img = m_pendingImage.toUtf8();
                    logWrite("Attached (window). image=%s opacity=%d",
                             img.isEmpty() ? "(empty)" : img.constData(), m_pendingOpacity);
                }
                return; // done
            }
            mo = mo->superClass();
        }
    }
}
