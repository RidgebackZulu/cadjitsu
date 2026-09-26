#include "viewport/Viewport.h"

#include "ui/Icons.h"
#include "viewport/NavBar.h"
#include "viewport/ViewCube.h"
#include "viewport/ViewportTool.h"

#include <rhi/qrhi.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <cmath>

namespace cadly {

namespace {

float niceStep(float approx) {
    const float p = std::pow(10.0f, std::floor(std::log10(std::max(approx, 1e-6f))));
    const float m = approx / p;
    if(m < 1.5f) return p;
    if(m < 3.5f) return 2.0f * p;
    if(m < 7.5f) return 5.0f * p;
    return 10.0f * p;
}

} // namespace

Viewport::Viewport(QWidget *parent) : QRhiWidget(parent) {
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setSampleCount(4);
    setAttribute(Qt::WA_AcceptTouchEvents, false);

    m_overlay = new ViewportOverlay(this);
    m_navBar = new NavBar(this);

    m_homeButton = new QToolButton(this);
    m_homeButton->setObjectName(QStringLiteral("viewCubeHome"));
    m_homeButton->setIcon(icon(IconId::Home));
    m_homeButton->setIconSize(QSize(16, 16));
    m_homeButton->setAutoRaise(true);
    m_homeButton->setToolTip(tr("Home view"));
    m_homeButton->setFocusPolicy(Qt::NoFocus);
    connect(m_homeButton, &QToolButton::clicked, this, [this] {
        Camera c = m_camera;
        c.rotation = Camera::orientationFor(StandardView::Home);
        c.fit(contentBounds());
        animateTo(c.rotation, c.target, c.distance);
    });

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(320);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::InOutCubic);

    m_camera.viewport = size();
    updateGrid();
}

Viewport::~Viewport() = default;

void Viewport::initialize(QRhiCommandBuffer *) {
    m_backendName = QString::fromLatin1(rhi()->backendName());
    m_renderer.initialize(rhi(), renderTarget()->renderPassDescriptor(), renderTarget()->sampleCount());
}

void Viewport::releaseResources() { m_renderer.releaseResources(); }

void Viewport::render(QRhiCommandBuffer *cb) {
    m_camera.viewport = size();
    RenderScene scene = m_content;
    scene.style = m_style;
    scene.grid = m_grid;
    scene.clipPlane = m_clip;
    scene.viewCubeHover = m_cubeHover;
    updateGrid();
    scene.gridExtent = m_content.gridExtent;
    scene.gridMinor = m_content.gridMinor;
    scene.gridMajor = m_content.gridMajor;
    scene.faceHighlights.insert(scene.faceHighlights.end(), m_faceHi.begin(), m_faceHi.end());
    scene.edgeHighlights.insert(scene.edgeHighlights.end(), m_edgeHi.begin(), m_edgeHi.end());
    scene.points.insert(scene.points.end(), m_pointHi.begin(), m_pointHi.end());
    scene.triangles.insert(scene.triangles.end(), m_triHi.begin(), m_triHi.end());
    if(m_tool) m_tool->contribute(scene);

    // Near / far planes around everything drawn, including tool geometry and
    // the grid wherever it is placed (the sketch plane while sketching).
    Box3 bounds = contentBounds();
    for(const auto &l : scene.lines)
        for(const auto &p : l.segments) bounds.add(p);
    for(const auto &pb : scene.points)
        for(const auto &p : pb.points) bounds.add(p);
    for(const auto &t : scene.triangles)
        for(const auto &p : t.triangles) bounds.add(p);
    if(scene.grid) {
        const float e = scene.gridExtent;
        for(const QVector3D &c : {QVector3D(-e, -e, 0), QVector3D(e, -e, 0), QVector3D(e, e, 0), QVector3D(-e, e, 0)})
            bounds.add(scene.gridFrame.map(c));
    }
    m_camera.updateClipPlanes(bounds);

    m_renderer.render(cb, renderTarget(), scene, m_camera, float(devicePixelRatioF()));
    ++m_frames;
}

void Viewport::refreshOverlay() {
    update();
    m_overlay->update();
}

void Viewport::setContent(const RenderScene &scene, std::vector<PickTarget> targets) {
    const float extent = m_content.gridExtent, minor = m_content.gridMinor, major = m_content.gridMajor;
    m_content = scene;
    m_content.gridExtent = extent;
    m_content.gridMinor = minor;
    m_content.gridMajor = major;
    m_targets = std::move(targets);
    if(m_firstFit && !m_targets.empty()) {
        m_firstFit = false;
        m_camera.viewport = size();
        m_camera.fit(contentBounds());
    }
    // The hovered entity may no longer exist.
    if(m_hover.valid()) {
        bool found = false;
        for(const auto &t : m_targets) found |= t.body == m_hover.body;
        if(!found) m_hover = PickHit();
    }
    update();
}

void Viewport::setHighlights(std::vector<FaceHighlight> faces, std::vector<EdgeHighlight> edges,
                             std::vector<PointBatch> points, std::vector<TriangleBatch> triangles) {
    m_faceHi = std::move(faces);
    m_edgeHi = std::move(edges);
    m_pointHi = std::move(points);
    m_triHi = std::move(triangles);
    update();
}

void Viewport::setDisplayStyle(DisplayStyle s) {
    m_style = s;
    update();
}

void Viewport::setGridVisible(bool on) {
    m_grid = on;
    m_navBar->syncFromViewport();
    update();
}

void Viewport::setOrthographic(bool on) {
    m_camera.orthographic = on;
    changed();
}

void Viewport::setClipPlane(std::optional<QVector4D> plane) {
    m_clip = plane;
    m_pickOptions.clipPlane = plane;
    update();
}

void Viewport::setTool(ViewportTool *tool) {
    m_tool = tool;
    setCursor(tool ? tool->cursor() : Qt::ArrowCursor);
    update();
    m_overlay->update();
}

void Viewport::setNavMode(NavMode m) {
    m_navMode = m;
    setCursor(m == NavMode::Select ? (m_tool ? m_tool->cursor() : Qt::ArrowCursor) : Qt::OpenHandCursor);
    m_navBar->syncFromViewport();
}

Box3 Viewport::contentBounds() const {
    Box3 b;
    for(const auto &t : m_targets)
        if(t.mesh) b.add(meshBounds(*t.mesh));
    for(const auto &l : m_content.lines)
        for(const auto &p : l.segments) b.add(p);
    for(const auto &pb : m_content.points)
        for(const auto &p : pb.points) b.add(p);
    for(const auto &t : m_content.triangles)
        for(const auto &p : t.triangles) b.add(p);
    return b;
}

void Viewport::updateGrid() {
    const float h = m_camera.viewHeightAtTarget();
    const float minor = niceStep(h / 24.0f);
    m_content.gridMinor = minor;
    m_content.gridMajor = minor * 10.0f;
    const Box3 b = contentBounds();
    const float modelExtent = b.isEmpty() ? 0.0f : std::max({std::fabs(b.min.x()), std::fabs(b.max.x()), std::fabs(b.min.y()),
                                                               std::fabs(b.max.y())});
    m_content.gridExtent = std::max({minor * 60.0f, modelExtent * 2.0f, h * 1.2f});
}

void Viewport::fitAll(bool animate) {
    Camera c = m_camera;
    c.viewport = size();
    c.fit(contentBounds());
    if(animate) animateTo(c.rotation, c.target, c.distance);
    else {
        m_camera = c;
        changed();
    }
}

void Viewport::setStandardView(StandardView v, bool animate) {
    Camera c = m_camera;
    c.rotation = Camera::orientationFor(v);
    if(v == StandardView::Home) c.fit(contentBounds());
    if(animate) animateTo(c.rotation, c.target, c.distance);
    else {
        m_camera = c;
        changed();
    }
}

void Viewport::stopAnimation() {
    m_anim->stop();
    disconnect(m_anim, nullptr, this, nullptr);
}

void Viewport::animateTo(const QQuaternion &rotation, const QVector3D &target, float distance) {
    stopAnimation();
    const QQuaternion r0 = m_camera.rotation;
    const QVector3D t0 = m_camera.target;
    const float d0 = m_camera.distance;
    connect(m_anim, &QVariantAnimation::valueChanged, this, [=, this](const QVariant &v) {
        const float s = v.toFloat();
        m_camera.rotation = QQuaternion::slerp(r0, rotation, s).normalized();
        m_camera.target = t0 + (target - t0) * s;
        m_camera.distance = d0 + (distance - d0) * s;
        changed();
    });
    m_anim->start();
}

PickHit Viewport::pickAt(QPointF px) const {
    Camera c = m_camera;
    c.viewport = size();
    return pick(c, px, m_targets, m_pickOptions);
}

std::optional<QVector3D> Viewport::raycast(QPointF px) const {
    Camera c = m_camera;
    c.viewport = size();
    return cadly::raycast(c, px, m_targets, m_clip);
}

QVector3D Viewport::anchorAt(QPointF px) const {
    if(auto hit = raycast(px)) return *hit;
    // Otherwise the point on the plane through the target facing the camera.
    QVector3D o, d;
    m_camera.ray(px, o, d);
    const QVector3D n = m_camera.forward();
    const float denom = QVector3D::dotProduct(d, n);
    if(std::fabs(denom) < 1e-6f) return m_camera.target;
    const float t = QVector3D::dotProduct(m_camera.target - o, n) / denom;
    return o + d * t;
}

void Viewport::changed() {
    update();
    m_overlay->update();
    emit cameraChanged();
}

void Viewport::updateHover(QPointF pos) {
    const auto cube = ViewCube::hitTest(pos, size(), m_camera.rotation);
    if(cube != m_cubeHover) {
        m_cubeHover = cube;
        update();
    }
    PickHit hit;
    hit.screen = pos;
    if(!cube && !ViewCube::rect(size()).contains(pos.toPoint())) hit = pickAt(pos);
    if(!hit.sameEntity(m_hover)) {
        m_hover = hit;
        emit hoverChanged(m_hover);
        update();
    } else {
        m_hover.point = hit.point;
        m_hover.screen = hit.screen;
    }
    emit hoverMoved(hit);
}

bool Viewport::event(QEvent *e) {
    if(e->type() == QEvent::NativeGesture) {
        auto *g = static_cast<QNativeGestureEvent *>(e);
        const QPointF pos = mapFromGlobal(g->globalPosition());
        if(g->gestureType() == Qt::ZoomNativeGesture) {
            stopAnimation();
            m_camera.zoom(float(1.0 + g->value()), anchorAt(pos));
            changed();
            return true;
        }
        if(g->gestureType() == Qt::SmartZoomNativeGesture) {
            fitAll();
            return true;
        }
    }
    return QRhiWidget::event(e);
}

void Viewport::mousePressEvent(QMouseEvent *e) {
    setFocus(Qt::MouseFocusReason);
    stopAnimation();
    const QPointF pos = e->position();
    m_pressPos = m_lastPos = pos;
    m_dragMoved = false;
    m_dragButton = e->button();

    if(e->button() == Qt::MiddleButton) {
        if(e->modifiers() & Qt::ShiftModifier) {
            m_drag = Drag::Orbit;
            m_pivot = raycast(pos).value_or(m_camera.target);
        } else {
            m_drag = Drag::Pan;
            m_pivot = anchorAt(pos);
        }
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if(e->button() == Qt::LeftButton) {
        if(ViewCube::rect(size()).contains(pos.toPoint())) {
            m_drag = Drag::Cube;
            m_pivot = m_camera.target;
            return;
        }
        switch(m_navMode) {
        case NavMode::Orbit:
            m_drag = Drag::Orbit;
            m_pivot = raycast(pos).value_or(m_camera.target);
            return;
        case NavMode::Pan:
            m_drag = Drag::Pan;
            m_pivot = anchorAt(pos);
            return;
        case NavMode::Zoom:
            m_drag = Drag::Zoom;
            m_pivot = anchorAt(pos);
            return;
        case NavMode::Select:
            break;
        }
        if(m_tool && m_tool->mousePress(e)) {
            m_drag = Drag::None;
            m_overlay->update();
            update();
            return;
        }
        m_drag = Drag::Box;
        m_box = QRectF(pos, pos);
        return;
    }
    if(e->button() == Qt::RightButton) {
        m_drag = Drag::None;
    }
}

void Viewport::mouseMoveEvent(QMouseEvent *e) {
    const QPointF pos = e->position();
    const QPointF delta = pos - m_lastPos;
    m_lastPos = pos;
    if((pos - m_pressPos).manhattanLength() > 4) m_dragMoved = true;

    switch(m_drag) {
    case Drag::Pan:
        m_camera.pan(delta, m_pivot);
        changed();
        return;
    case Drag::Orbit:
    case Drag::Cube:
        if(m_drag == Drag::Cube && !m_dragMoved) return;
        m_camera.orbit(float(delta.x()), float(delta.y()), m_pivot);
        changed();
        return;
    case Drag::Zoom:
        m_camera.zoom(std::pow(1.01f, float(-delta.y())), m_pivot);
        changed();
        return;
    case Drag::Box:
        if(m_dragMoved) {
            m_box = QRectF(m_pressPos, pos).normalized();
            m_overlay->update();
        }
        return;
    case Drag::None:
        break;
    }
    if(m_tool && m_tool->mouseMove(e)) {
        m_overlay->update();
        update();
    }
    updateHover(pos);
}

void Viewport::mouseReleaseEvent(QMouseEvent *e) {
    const QPointF pos = e->position();
    const Drag drag = m_drag;
    m_drag = Drag::None;
    setCursor(m_navMode != NavMode::Select ? Qt::OpenHandCursor : (m_tool ? m_tool->cursor() : Qt::ArrowCursor));

    if(e->button() == Qt::RightButton && !m_dragMoved) {
        emit contextMenuRequested(e->globalPosition().toPoint(), pickAt(pos));
        return;
    }
    if(e->button() != Qt::LeftButton) return;

    if(drag == Drag::Cube && !m_dragMoved) {
        if(auto region = ViewCube::hitTest(pos, size(), m_camera.rotation)) {
            Camera c = m_camera;
            c.rotation = ViewCube::orientationForRegion(*region);
            c.fit(contentBounds());
            animateTo(c.rotation, c.target, c.distance);
        }
        return;
    }
    if(drag == Drag::Box) {
        if(m_dragMoved) {
            const bool crossing = pos.x() < m_pressPos.x();
            const QRectF rect = m_box;
            m_box = QRectF();
            m_overlay->update();
            emit boxSelected(rect, crossing, e->modifiers());
        } else {
            emit clicked(pickAt(pos), e->modifiers());
        }
        return;
    }
    if(drag == Drag::None && m_tool && m_tool->mouseRelease(e)) {
        m_overlay->update();
        update();
    }
}

void Viewport::mouseDoubleClickEvent(QMouseEvent *e) {
    if(e->button() == Qt::MiddleButton) {
        fitAll();
        return;
    }
    if(e->button() == Qt::LeftButton) {
        if(m_tool && m_tool->mouseDoubleClick(e)) {
            update();
            m_overlay->update();
            return;
        }
        emit doubleClicked(pickAt(e->position()));
    }
}

void Viewport::wheelEvent(QWheelEvent *e) {
    stopAnimation();
    const QPointF pos = e->position();
    const bool trackpad = !e->pixelDelta().isNull() && e->phase() != Qt::NoScrollPhase;
    if(trackpad && !(e->modifiers() & Qt::ControlModifier)) {
        // Two-finger drag pans (Shift orbits), like Fusion's trackpad mode.
        const QPointF d = e->pixelDelta();
        if(e->modifiers() & Qt::ShiftModifier) m_camera.orbit(float(-d.x()), float(-d.y()), raycast(pos).value_or(m_camera.target));
        else m_camera.pan(d, anchorAt(pos));
        changed();
        e->accept();
        return;
    }
    float steps = float(e->angleDelta().y());
    if(steps == 0.0f) steps = float(e->angleDelta().x());
    if(steps == 0.0f && trackpad) steps = float(e->pixelDelta().y()) * 2.0f;
    m_camera.zoom(std::pow(1.0015f, steps), anchorAt(pos));
    changed();
    e->accept();
}

void Viewport::keyPressEvent(QKeyEvent *e) {
    if(m_tool && m_tool->keyPress(e)) {
        update();
        m_overlay->update();
        return;
    }
    switch(e->key()) {
    case Qt::Key_F6:
        fitAll();
        return;
    case Qt::Key_Escape:
        if(m_navMode != NavMode::Select) setNavMode(NavMode::Select);
        emit escapePressed();
        return;
    default:
        QRhiWidget::keyPressEvent(e);
    }
}

void Viewport::leaveEvent(QEvent *e) {
    if(m_hover.valid() || m_cubeHover) {
        m_hover = PickHit();
        m_cubeHover.reset();
        emit hoverChanged(m_hover);
        update();
    }
    PickHit gone;
    gone.screen = QPointF(-1e6, -1e6);
    emit hoverMoved(gone);
    QRhiWidget::leaveEvent(e);
}

void Viewport::resizeEvent(QResizeEvent *e) {
    QRhiWidget::resizeEvent(e);
    m_camera.viewport = size();
    m_overlay->setGeometry(rect());
    m_navBar->reposition();
    const QRect cube = ViewCube::rect(size());
    m_homeButton->move(cube.left() - 6, cube.top() - 4);
    m_homeButton->raise();
}

void Viewport::paintOverlay(QPainter &p) {
    if(!m_box.isNull()) {
        const bool crossing = m_lastPos.x() < m_pressPos.x();
        QPen pen(crossing ? QColor(40, 150, 70) : QColor(40, 110, 210), 1.2, crossing ? Qt::DashLine : Qt::SolidLine);
        p.setPen(pen);
        p.setBrush(crossing ? QColor(60, 180, 90, 35) : QColor(60, 130, 220, 35));
        p.drawRect(m_box);
    }
    if(m_tool) m_tool->paintOverlay(p);
}

} // namespace cadly
