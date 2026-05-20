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

#include <windows.h>

// ─── OverlayWidget ───────────────────────────────────────────────────

OverlayWidget::OverlayWidget(QWidget *parent)
    : QWidget(parent)
{
    // Mouse events pass straight through to whatever is behind us
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // Visual transparency: only the pixels we paint are visible
    setAttribute(Qt::WA_TranslucentBackground);
    // Don't accept keyboard focus — keystrokes go to the editor
    setFocusPolicy(Qt::NoFocus);
    // CRITICAL: Force a native HWND.  QScintilla may use native child
    // windows for the editing surface, which always paint above non-native
    // Qt widgets.  A native overlay can be stacked above them.
    setAttribute(Qt::WA_NativeWindow);

    // Ensure we are on top of sibling native windows
    raise();
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

    // ── Scale the image ──────────────────────────────────────
    QImage scaled;
    switch (m_scaleMode) {
    case 0: // Fit — scale to fit, keep aspect
        scaled = m_image.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        break;
    case 1: // Fill — scale and crop to fill (default)
        scaled = m_image.scaled(sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        break;
    case 2: // Stretch — ignore aspect ratio
        scaled = m_image.scaled(sz, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        break;
    case 3: // Center — no scaling, centered
        scaled = QImage(sz, QImage::Format_ARGB32);
        scaled.fill(Qt::transparent);
        {
            QPainter sp(&scaled);
            int ox = (sz.width()  - m_image.width())  / 2;
            int oy = (sz.height() - m_image.height()) / 2;
            sp.drawImage(ox, oy, m_image);
        }
        break;
    case 4: // Tile — repeat
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

    // ── Draw image with user opacity ─────────────────────────
    painter.setOpacity(m_opacity / 100.0);

    int ox = (sz.width()  - scaled.width())  / 2;
    int oy = (sz.height() - scaled.height()) / 2;
    painter.drawImage(ox, oy, scaled);

    // ── Dimming overlay on top ───────────────────────────────
    painter.setOpacity(1.0);
    if (m_dimming > 0) {
        painter.fillRect(rect(), QColor(0, 0, 0, m_dimming * 255 / 100));
    }
}

// ─── BackgroundImageEffect ───────────────────────────────────────────

BackgroundImageEffect::BackgroundImageEffect(QObject *parent)
    : QObject(parent) {}

BackgroundImageEffect::~BackgroundImageEffect()
{
    detach();
    // If editor is still alive, remove event filter
    if (m_editor) {
        m_editor->removeEventFilter(this);
        auto *area = static_cast<QAbstractScrollArea*>(m_editor.data());
        if (area && area->viewport())
            area->viewport()->removeEventFilter(this);
    }
}

void BackgroundImageEffect::attach(QWidget *editorWidget)
{
    if (m_editor == editorWidget) return;
    detach();
    m_editor = editorWidget;
    if (!m_editor) return;

    auto *area = static_cast<QAbstractScrollArea*>(m_editor.data());
    QWidget *vp = area ? area->viewport() : nullptr;
    if (!vp) {
        logWrite("WARN: No viewport found for editor %p", (void*)m_editor);
        return;
    }

    // IMPORTANT: Parent the overlay to the VIEWPORT, not the outer
    // QAbstractScrollArea.  The viewport covers the entire visible area
    // and QAbstractScrollArea manages it as a special child that always
    // sits on top of ordinary children.  By parenting to the viewport
    // we guarantee the overlay paints above Scintilla's content.
    QSize vs = area->maximumViewportSize();
    if (vs.isEmpty()) vs = vp->size();

    m_overlay = new OverlayWidget(vp);
    m_overlay->resize(vs);
    m_overlay->move(0, 0);
    m_overlay->show();
    m_overlay->raise();

    // Win32: force overlay HWND to the top of the z-order
    if (m_overlay->internalWinId()) {
        SetWindowPos(reinterpret_cast<HWND>(m_overlay->winId()),
                     HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    logWrite("Overlay created: size=(%d,%d) vpSize=(%d,%d) maxVp=(%d,%d) imageNull=%d opacity=%d dimming=%d",
             vs.width(), vs.height(),
             vp->size().width(), vp->size().height(),
             area->maximumViewportSize().width(), area->maximumViewportSize().height(),
             (int)m_image.isNull(), m_opacity, m_dimming);

    updateOverlay();

    // Track resize on both the editor and the viewport
    m_editor->installEventFilter(this);
    vp->installEventFilter(this);

    // Track scroll changes: keep the overlay covering the visible area
    if (area->verticalScrollBar()) {
        QObject::connect(area->verticalScrollBar(), &QScrollBar::valueChanged,
                         [this](int) {
            repositionOverlay();
        });
    }
    if (area->horizontalScrollBar()) {
        QObject::connect(area->horizontalScrollBar(), &QScrollBar::valueChanged,
                         [this](int) {
            repositionOverlay();
        });
    }
}

void BackgroundImageEffect::detach()
{
    // m_overlay is a child widget — it may already be deleted by Qt
    // when the editor/viewport was destroyed.  QPointer handles this.
    if (m_overlay) {
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
    if (m_editor) {
        m_editor->removeEventFilter(this);
        auto *area = static_cast<QAbstractScrollArea*>(m_editor.data());
        if (area && area->viewport()) {
            area->viewport()->removeEventFilter(this);
        }
        m_editor = nullptr;
    }
}

void BackgroundImageEffect::repositionOverlay()
{
    if (!m_overlay || !m_editor) return;
    auto *area = static_cast<QAbstractScrollArea*>(m_editor.data());
    if (!area || !area->viewport()) return;

    // Overlay lives in viewport coordinates.
    // When scrolled, the viewport shifts to (-sx, -sy) relative to the
    // scroll area.  We place the overlay at (+sx, +sy) in viewport
    // coords so it stays at the origin of the VISIBLE area.
    int sx = area->horizontalScrollBar() ? area->horizontalScrollBar()->value() : 0;
    int sy = area->verticalScrollBar()   ? area->verticalScrollBar()->value()   : 0;

    // Size must be the VISIBLE viewport area, not the full content extent.
    // maximumViewportSize() gives the space available (minus scrollbars).
    QSize vs = area->maximumViewportSize();
    if (vs.isEmpty()) vs = area->viewport()->size();

    m_overlay->resize(vs);
    m_overlay->move(sx, sy);
    m_overlay->raise();

    // Force Win32 z-order: ensure our native HWND sits above the Scintilla
    // native child window (if any).
    if (m_overlay->internalWinId()) {
        SetWindowPos(reinterpret_cast<HWND>(m_overlay->winId()),
                     HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

bool BackgroundImageEffect::eventFilter(QObject *obj, QEvent *event)
{
    // If either the editor or overlay was already destroyed, bail out.
    if (!m_editor || !m_overlay)
        return false;

    // Editor/viewport being destroyed — clean up our references
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

    // Resize / Show — keep overlay covering the editor
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        repositionOverlay();
    }
    return false;
}

void BackgroundImageEffect::updateOverlay()
{
    if (m_overlay) {
        m_overlay->setImage(m_image, m_opacity, m_dimming, m_scaleMode);
        m_overlay->raise();

        // Ensure native HWND stay on top of Scintilla native windows
        if (m_overlay->internalWinId()) {
            SetWindowPos(reinterpret_cast<HWND>(m_overlay->winId()),
                         HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
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
    // QSettings INI parser treats backslash as escape char.
    // patcher.exe now saves with forward slashes; convert back for Windows.
    imagePath.replace(QLatin1Char('/'), QLatin1Char('\\'));
    int opacity   = settings.value("Background/Opacity",   30).toInt();
    int dimming   = settings.value("Background/Dimming",   30).toInt();
    int scaleMode = settings.value("Background/ScaleMode", 1).toInt();

    for (auto *fx : m_effects) {
        if (fx->image().isNull() && imagePath.isEmpty())
            continue; // nothing to update
        fx->setImage(imagePath);
        fx->setOpacity(opacity);
        fx->setDimming(dimming);
        fx->setScaleMode(scaleMode);
    }

    m_pendingImage     = imagePath;
    m_pendingOpacity   = opacity;
    m_pendingDimming   = dimming;
    m_pendingScaleMode = scaleMode;
}

void BackgroundManager::scanAndAttach()
{
    static bool firstScan = true;

    // Remove effects whose editors have been closed
    for (int i = m_effects.size() - 1; i >= 0; i--) {
        if (!m_effects[i]->isAlive()) {
            delete m_effects[i];
            m_effects.removeAt(i);
            m_attached.removeAt(i);
        }
    }

    QWidgetList all = QApplication::allWidgets();
    for (QWidget *w : all) {
        if (m_attached.contains(w)) continue;

        const QMetaObject *mo = w->metaObject();
        bool isScintilla = false;
        QString matchedName;

        while (mo) {
            QString cn = QString::fromLatin1(mo->className());
            // QsciScintilla may also appear under namespaced subclasses
            if (cn == QLatin1String("QsciScintilla") ||
                cn == QLatin1String("QsciScintillaBase") ||
                cn.contains(QLatin1String("Scintilla"))) {
                isScintilla = true;
                matchedName = cn;
                break;
            }
            mo = mo->superClass();
        }

        // First scan: dump widget tree for diagnostics
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
            logWrite("FOUND Scintilla editor: %p  class=%s  size=(%d,%d)  visible=%d",
                     (void*)w, mn.constData(),
                     w->size().width(), w->size().height(),
                     (int)w->isVisible());

            auto *fx = new BackgroundImageEffect(this);
            fx->setImage(m_pendingImage);
            fx->setOpacity(m_pendingOpacity);
            fx->setDimming(m_pendingDimming);
            fx->setScaleMode(m_pendingScaleMode);
            fx->attach(w);

            m_effects.append(fx);
            m_attached.append(w);

            QByteArray img = m_pendingImage.toUtf8();
            logWrite("Attached overlay. image=%s opacity=%d",
                     img.isEmpty() ? "(empty)" : img.constData(),
                     m_pendingOpacity);
        }
    }
    firstScan = false;
}
