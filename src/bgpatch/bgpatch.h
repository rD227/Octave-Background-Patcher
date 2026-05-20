#ifndef BGPATCH_H
#define BGPATCH_H

#include <QObject>
#include <QImage>
#include <QWidget>
#include <QTimer>
#include <QSettings>
#include <QPointer>

// Shared logging (implemented in main.cpp, used by all translation units)
extern void logWrite(const char *fmt, ...);

// ─── Transparent overlay widget ─────────────────────────────────────
// Sits ON TOP of the editor.  Draws the background image, but all
// mouse and keyboard input passes straight through to the editor.
//
// WA_TransparentForMouseEvents handles mouse passthrough.
// setFocusPolicy(Qt::NoFocus) handles keyboard passthrough.
class OverlayWidget : public QWidget
{
public:
    explicit OverlayWidget(QWidget *parent = nullptr);

    void setImage(const QImage &img, int opacity, int dimming, int scaleMode);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
    int    m_opacity   = 30;
    int    m_dimming   = 30;
    int    m_scaleMode = 1;
};

// ─── Background effect manager ──────────────────────────────────────

class BackgroundImageEffect : public QObject
{
public:
    explicit BackgroundImageEffect(QObject *parent = nullptr);
    ~BackgroundImageEffect();

    void setImage(const QString &path);
    void setOpacity(int percent);
    void setDimming(int percent);
    void setScaleMode(int mode);

    void attach(QWidget *editorWidget);
    void detach();
    bool isAlive() const { return !m_editor.isNull(); }

    const QImage &image() const { return m_image; }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void reloadImage();
    void updateOverlay();
    void repositionOverlay();

    QImage  m_image;
    QString m_imagePath;
    int     m_opacity   = 30;
    int     m_dimming   = 30;
    int     m_scaleMode = 1;

    QPointer<QWidget>        m_editor;
    QPointer<OverlayWidget>  m_overlay;
};

// ─── Global manager (singleton) ─────────────────────────────────────

class BackgroundManager : public QObject
{
public:
    static BackgroundManager *instance();

    void init();
    void reloadSettings();
    void applyToAll();

private:
    explicit BackgroundManager(QObject *parent = nullptr);
    void scanAndAttach();

    QTimer *m_scanTimer = nullptr;
    QList<BackgroundImageEffect*> m_effects;
    QList<QWidget*> m_attached;

    QString m_pendingImage;
    int m_pendingOpacity   = 30;
    int m_pendingDimming   = 30;
    int m_pendingScaleMode = 1;
};

#endif // BGPATCH_H
