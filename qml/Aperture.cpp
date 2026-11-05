#include "Aperture.hpp"

#include "hyprture-v1-client-protocol.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <qpa/qplatformnativeinterface.h>

#include <cmath>
#include <cstring>
#include <wayland-client.h>

namespace {
    // The hyprture_manager_v1 global, bound on Qt's own connection. Registry
    // events come through Qt's dispatch, on the GUI thread.
    struct SGlobals {
        wl_display*          display  = nullptr;
        wl_registry*         registry = nullptr;
        hyprture_manager_v1* manager  = nullptr;
        uint32_t             name     = 0;
        QList<Aperture*>     items;
    };

    SGlobals& globals() {
        static SGlobals g;
        return g;
    }

    void notifyAll() {
        for (auto* item : globals().items) {
            emit item->availableChanged();
            item->requestSync();
        }
    }

    void onGlobal(void*, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
        auto& g = globals();
        if (g.manager || std::strcmp(interface, hyprture_manager_v1_interface.name) != 0)
            return;
        g.manager = static_cast<hyprture_manager_v1*>(wl_registry_bind(registry, name, &hyprture_manager_v1_interface, 1));
        g.name    = name;
        notifyAll();
    }

    void onGlobalRemove(void*, wl_registry*, uint32_t name) {
        auto& g = globals();
        if (!g.manager || name != g.name)
            return;
        // The compositor dropped hyprture (plugin unloaded): its objects are
        // gone with it, so a request on one would be a protocol error.
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(g.manager));
        g.manager = nullptr;
        g.name    = 0;
        for (auto* item : g.items)
            item->forgetProxy();
        notifyAll();
    }

    const wl_registry_listener REGISTRY_LISTENER = {.global = onGlobal, .global_remove = onGlobalRemove};

    hyprture_manager_v1* manager() {
        auto& g = globals();
        if (!g.registry) {
            auto* native = QGuiApplication::platformNativeInterface();
            g.display    = native ? static_cast<wl_display*>(native->nativeResourceForIntegration("wl_display")) : nullptr;
            if (!g.display)
                return nullptr;
            g.registry = wl_display_get_registry(g.display);
            wl_registry_add_listener(g.registry, &REGISTRY_LISTENER, nullptr);
            wl_display_flush(g.display);
        }
        return g.manager;
    }

    wl_surface* surfaceOf(QQuickWindow* window) {
        auto* native = QGuiApplication::platformNativeInterface();
        if (!native || !window || !window->isVisible() || !window->handle())
            return nullptr;
        return static_cast<wl_surface*>(native->nativeResourceForWindow("surface", window));
    }

    void onSourceEvent(void* data, hyprture_aperture_v1*, int32_t width, int32_t height) {
        static_cast<Aperture*>(data)->onSource(width, height);
    }

    const hyprture_aperture_v1_listener APERTURE_LISTENER = {.source = onSourceEvent};

    // The opacity it is drawn with: its own times its ancestors'.
    qreal effectiveOpacity(const QQuickItem* item) {
        qreal opacity = 1;
        for (; item; item = item->parentItem())
            opacity *= item->opacity();
        return opacity;
    }
}

Aperture::Aperture(QQuickItem* parent) : QQuickItem(parent) {
    globals().items.append(this);
    manager();
}

Aperture::~Aperture() {
    globals().items.removeAll(this);
    destroyProxy();
}

int Aperture::workspace() const {
    return m_kind == Kind::Workspace ? m_workspace : -1;
}

void Aperture::setWorkspace(int id) {
    if (m_kind == Kind::Workspace && m_workspace == id)
        return;
    m_workspace = id;
    m_kind      = id >= 0 ? Kind::Workspace : Kind::None;
    sourceEdited();
}

QString Aperture::windowAddress() const {
    return m_kind == Kind::Window ? m_window : QString{};
}

void Aperture::setWindowAddress(const QString& address) {
    if (m_kind == Kind::Window && m_window == address)
        return;
    m_window = address;
    m_kind   = address.isEmpty() ? Kind::None : Kind::Window;
    sourceEdited();
}

QString Aperture::monitor() const {
    return m_kind == Kind::Monitor ? m_monitor : QString{};
}

void Aperture::setMonitor(const QString& name) {
    if (m_kind == Kind::Monitor && m_monitor == name)
        return;
    m_monitor = name;
    m_kind    = name.isEmpty() ? Kind::None : Kind::Monitor;
    sourceEdited();
}

Aperture::Scene Aperture::scene() const {
    return m_scene;
}

void Aperture::setScene(Scene scene) {
    if (m_scene == scene)
        return;
    m_scene = scene;
    sourceEdited();
}

qreal Aperture::radius() const {
    return m_radius;
}

void Aperture::setRadius(qreal radius) {
    if (qFuzzyCompare(m_radius, radius))
        return;
    m_radius = radius;
    emit radiusChanged();
    requestSync();
}

bool Aperture::live() const {
    return m_live;
}

void Aperture::setLive(bool live) {
    if (m_live == live)
        return;
    m_live = live;
    emit liveChanged();
    requestSync();
}

QSizeF Aperture::sourceSize() const {
    return m_sourceSize;
}

bool Aperture::available() const {
    return globals().manager;
}

void Aperture::onSource(int width, int height) {
    const QSizeF SIZE = width > 0 && height > 0 ? QSizeF(width, height) : QSizeF();
    if (SIZE == m_sourceSize)
        return;
    m_sourceSize = SIZE;
    emit sourceSizeChanged();
}

void Aperture::sourceEdited() {
    m_sent.source = false;
    emit sourceChanged();
    requestSync();
}

void Aperture::itemChange(ItemChange change, const ItemChangeData& data) {
    if (change == ItemSceneChange)
        attach(data.window);
    else if (change == ItemVisibleHasChanged || change == ItemOpacityHasChanged)
        requestSync();
    QQuickItem::itemChange(change, data);
}

void Aperture::geometryChange(const QRectF& next, const QRectF& previous) {
    QQuickItem::geometryChange(next, previous);
    requestSync();
}

void Aperture::requestSync() {
    if (m_quickWindow)
        m_quickWindow->update();
}

void Aperture::attach(QQuickWindow* window) {
    if (m_quickWindow == window)
        return;
    QObject::disconnect(m_frameConnection);
    destroyProxy();
    m_quickWindow = window;
    if (!window)
        return;
    // Before every frame of the window: what changed goes with its commit.
    m_frameConnection = connect(window, &QQuickWindow::afterAnimating, this, &Aperture::sync);
    // Hiding destroys the window's wl_surface, and the aperture on it.
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible)
            destroyProxy();
    });
    window->update();
}

void Aperture::destroyProxy() {
    if (m_proxy)
        hyprture_aperture_v1_destroy(m_proxy);
    m_proxy   = nullptr;
    m_surface = nullptr;
    m_sent    = {};
}

void Aperture::forgetProxy() {
    if (m_proxy)
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(m_proxy));
    m_proxy   = nullptr;
    m_surface = nullptr;
    m_sent    = {};
}

void Aperture::sync() {
    auto* mgr = manager();
    if (!mgr)
        return;

    auto* surface = surfaceOf(m_quickWindow);
    if (surface != m_surface)
        destroyProxy();
    if (!surface)
        return;
    if (!m_proxy) {
        m_surface = surface;
        m_proxy   = hyprture_manager_v1_get_aperture(mgr, surface);
        hyprture_aperture_v1_add_listener(m_proxy, &APERTURE_LISTENER, this);
    }

    if (!m_sent.source) {
        switch (m_kind) {
            case Kind::None: hyprture_aperture_v1_unset_source(m_proxy); break;
            case Kind::Workspace:
                hyprture_aperture_v1_set_source_workspace(m_proxy, m_workspace,
                                                          m_scene == Shell ? HYPRTURE_APERTURE_V1_SCENE_SHELL : HYPRTURE_APERTURE_V1_SCENE_WINDOWS);
                break;
            case Kind::Window: hyprture_aperture_v1_set_source_window(m_proxy, m_window.toUtf8().constData()); break;
            case Kind::Monitor: hyprture_aperture_v1_set_source_monitor(m_proxy, m_monitor.toUtf8().constData()); break;
        }
        m_sent.source = true;
    }

    const QRectF R    = isVisible() ? mapRectToScene(QRectF(0, 0, width(), height())) : QRectF();
    const QRect  DEST = R.isEmpty() ? QRect() : QRect(qRound(R.x()), qRound(R.y()), qRound(R.width()), qRound(R.height()));
    if (DEST != m_sent.destination) {
        hyprture_aperture_v1_set_destination(m_proxy, DEST.x(), DEST.y(), std::max(0, DEST.width()), std::max(0, DEST.height()));
        m_sent.destination = DEST;
    }

    const qreal OPACITY = std::clamp(effectiveOpacity(this), 0.0, 1.0);
    if (std::abs(OPACITY - m_sent.opacity) > 1.0 / 512) {
        hyprture_aperture_v1_set_opacity(m_proxy, wl_fixed_from_double(OPACITY));
        m_sent.opacity = OPACITY;
    }
    if (m_radius != m_sent.radius) {
        hyprture_aperture_v1_set_radius(m_proxy, wl_fixed_from_double(m_radius));
        m_sent.radius = m_radius;
    }
    if (int(m_live) != m_sent.live) {
        hyprture_aperture_v1_set_live(m_proxy, m_live ? 1 : 0);
        m_sent.live = m_live;
    }
}
