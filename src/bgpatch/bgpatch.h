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

// Scope: where to apply the background image
enum Scope {
    ScopeEditor = 1,  // each editor pane individually
    ScopeWindow = 2   // entire Octave window
};

// ─── Transparent overlay widget ─────────────────────────────────────
// Sits ON TOP of the target.  Draws the background image, but all
// mouse and keyboard input passes straight through.
//
// WA_TransparentForMouseEvents handles mouse passthrough.
// setFocusPolicy(Qt::NoFocus) handles keyboard passthrough.
class OverlayWidget : public QWidget
{
public:
    explicit OverlayWidget(bool useNative, QWidget *parent = nullptr);

    void setImage(const QImage &img, int opacity, int dimming, int scaleMode);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *e) override;

private:
    QImage m_image;
    int    m_opacity   = 30;
    int    m_dimming   = 30;
    int    m_scaleMode = 1;
    bool   m_native    = false;
};

// ─── Background effect manager ──────────────────────────────────────

class BackgroundImageEffect : public QObject
{
public:
    explicit BackgroundImageEffect(Scope scope, QObject *parent = nullptr);
    ~BackgroundImageEffect();

    void setImage(const QString &path);
    void setOpacity(int percent);
    void setDimming(int percent);
    void setScaleMode(int mode);

    // Attach overlay to a QsciScintilla editor (ScopeEditor)
    void attachToEditor(QWidget *editorWidget);
    // Attach a single overlay to the main window (ScopeWindow)
    void attachToWindow(QWidget *mainWindow);

    void detach();
    Scope scope() const { return m_scope; }
    bool isAlive() const;

    const QImage &image() const { return m_image; }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void reloadImage();
    void updateOverlay();
    void repositionOverlay();

    Scope   m_scope;
    QImage  m_image;
    QString m_imagePath;
    int     m_opacity   = 30;
    int     m_dimming   = 30;
    int     m_scaleMode = 1;

    // Editor mode: editor = QsciScintilla, overlay parented to its viewport
    // Window mode: editor = QMainWindow, overlay parented directly to it
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
    void attachEditors();
    void attachWindow();

    QTimer *m_scanTimer = nullptr;
    QList<BackgroundImageEffect*> m_effects;
    QList<QWidget*> m_attached;
    Scope m_currentScope = ScopeEditor;

    QString m_pendingImage;
    int m_pendingOpacity   = 30;
    int m_pendingDimming   = 30;
    int m_pendingScaleMode = 1;
};

#endif // BGPATCH_H
