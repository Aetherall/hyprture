#pragma once

// import Hyprture
//
// Aperture { workspace: 6 }: Hyprland draws workspace 6 live in this item's
// rectangle, on top of the window's own content (hyprture_v1). The window
// must be a layer surface (a Quickshell PanelWindow): Hyprland draws
// apertures with their layer.

#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QSizeF>

struct hyprture_aperture_v1;
struct wl_surface;

class Aperture : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    // What it shows: one of workspace (an id, -1 for none), window (an
    // address, "0x…"), monitor (a name), whichever was set last.
    Q_PROPERTY(int workspace READ workspace WRITE setWorkspace NOTIFY sourceChanged)
    Q_PROPERTY(QString window READ windowAddress WRITE setWindowAddress NOTIFY sourceChanged)
    Q_PROPERTY(QString monitor READ monitor WRITE setMonitor NOTIFY sourceChanged)
    // A workspace's windows only, or with its monitor's wallpaper and layers.
    Q_PROPERTY(Scene scene READ scene WRITE setScene NOTIFY sourceChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
    // The source's logical size (its monitor's for a workspace), so the item
    // can take its aspect ratio; empty while the source doesn't exist. The
    // view is fitted inside the item, centered.
    Q_PROPERTY(QSizeF sourceSize READ sourceSize NOTIFY sourceSizeChanged)
    // Whether the compositor offers apertures (hyprture loaded).
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

  public:
    enum Scene {
        Windows = 0,
        Shell   = 1,
    };
    Q_ENUM(Scene)

    explicit Aperture(QQuickItem* parent = nullptr);
    ~Aperture() override;

    int     workspace() const;
    void    setWorkspace(int id);
    QString windowAddress() const;
    void    setWindowAddress(const QString& address);
    QString monitor() const;
    void    setMonitor(const QString& name);
    Scene   scene() const;
    void    setScene(Scene scene);
    qreal   radius() const;
    void    setRadius(qreal radius);
    bool    live() const;
    void    setLive(bool live);
    QSizeF  sourceSize() const;
    bool    available() const;

    // From the protocol listener.
    void onSource(int width, int height);
    // Have the window render a frame, which syncs.
    void requestSync();
    // hyprture went away (the global was removed): its objects are gone on
    // the compositor's side, so ours go without a request.
    void forgetProxy();

  signals:
    void sourceChanged();
    void radiusChanged();
    void liveChanged();
    void sourceSizeChanged();
    void availableChanged();

  protected:
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    void geometryChange(const QRectF& next, const QRectF& previous) override;

  private:
    enum class Kind {
        None,
        Workspace,
        Window,
        Monitor,
    };

    // Follows the window's frames: before each, pushes whatever changed
    // (place, opacity, source) to the compositor, which applies it with the
    // commit of that frame.
    void sync();
    void attach(QQuickWindow* window);
    void destroyProxy();
    void sourceEdited();

    Kind                  m_kind      = Kind::None;
    int                   m_workspace = -1;
    QString               m_window;
    QString               m_monitor;
    Scene                 m_scene  = Windows;
    qreal                 m_radius = 0;
    bool                  m_live   = true;
    QSizeF                m_sourceSize;

    QPointer<QQuickWindow> m_quickWindow;
    QMetaObject::Connection m_frameConnection;
    hyprture_aperture_v1*  m_proxy   = nullptr;
    wl_surface*            m_surface = nullptr;

    // What the compositor has, to send only changes.
    struct SSent {
        bool   source = false;
        QRect  destination;
        qreal  opacity = -1;
        qreal  radius  = -1;
        int    live    = -1;
    } m_sent;
};
