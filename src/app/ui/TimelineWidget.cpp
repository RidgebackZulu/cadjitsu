#include "ui/TimelineWidget.h"

#include "ui/Icons.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>

namespace cadly {

namespace {

constexpr int kItem = 30;       // feature icon size
constexpr int kGap = 6;         // space between icons
constexpr int kLeft = 142;      // playback buttons area
constexpr int kTop = 7;

IconId iconFor(cad::FeatureType t) {
    switch(t) {
    case cad::FeatureType::Sketch: return IconId::Sketch;
    case cad::FeatureType::Extrude: return IconId::Extrude;
    case cad::FeatureType::Fillet: return IconId::Fillet;
    case cad::FeatureType::Chamfer: return IconId::Chamfer;
    case cad::FeatureType::Hole: return IconId::Hole;
    case cad::FeatureType::Combine: return IconId::Combine;
    case cad::FeatureType::ConstructionPlane: return IconId::Plane;
    }
    return IconId::Body;
}

QToolButton *playButton(QWidget *parent, IconId id, const QString &tip, const char *name) {
    auto *b = new QToolButton(parent);
    b->setObjectName(QString::fromLatin1(name));
    b->setIcon(icon(id));
    b->setIconSize(QSize(16, 16));
    b->setAutoRaise(true);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

} // namespace

TimelineWidget::TimelineWidget(cad::Document &doc, QWidget *parent) : QWidget(parent), m_doc(doc) {
    setObjectName(QStringLiteral("timeline"));
    setMouseTracking(true);
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("#timeline { background: #eef0f3; border-top: 1px solid #c9ced6; }"
                                 "QToolButton { border-radius: 3px; padding: 2px; color: #1c2128; }"
                                 "QToolButton:hover { background: rgba(40, 110, 200, 35); }"));
    m_first = playButton(this, IconId::TimelineFirst, tr("Go to the beginning"), "timelineFirst");
    m_back = playButton(this, IconId::TimelineBack, tr("Step back"), "timelineBack");
    m_forward = playButton(this, IconId::TimelineForward, tr("Step forward"), "timelineForward");
    m_last = playButton(this, IconId::TimelineLast, tr("Go to the end"), "timelineLast");
    connect(m_first, &QToolButton::clicked, this, [this] { m_doc.setMarker(0); });
    connect(m_back, &QToolButton::clicked, this, [this] { m_doc.setMarker(m_doc.marker() - 1); });
    connect(m_forward, &QToolButton::clicked, this, [this] { m_doc.setMarker(m_doc.marker() + 1); });
    connect(m_last, &QToolButton::clicked, this, [this] { m_doc.setMarker(int(m_doc.features().size())); });
}

QSize TimelineWidget::sizeHint() const { return QSize(600, kItem + 2 * kTop + 2); }

void TimelineWidget::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    const int y = (height() - 24) / 2;
    int x = 10;
    for(QToolButton *b : {m_first, m_back, m_forward, m_last}) {
        b->setGeometry(x, y, 24, 24);
        x += 28;
    }
    clampScroll();
}

void TimelineWidget::setEvaluation(EvaluationPtr e) {
    m_eval = std::move(e);
    update();
}

void TimelineWidget::refresh() {
    if(m_selected >= count()) m_selected = -1;
    if(m_hover >= count()) m_hover = -1;
    clampScroll();
    // Keep the marker in view.
    const int mx = boundaryX(shownMarker());
    if(mx > width() - 20) m_scroll += mx - (width() - 40);
    if(mx < kLeft + 10) m_scroll -= (kLeft + 30) - mx;
    clampScroll();
    update();
}

void TimelineWidget::setEditing(cad::FeatureId id) {
    m_editing = id;
    update();
}

int TimelineWidget::shownMarker() const {
    if(m_editing != cad::kNoFeature) {
        const int i = m_doc.indexOf(m_editing);
        if(i >= 0) return i + 1;
    }
    return m_doc.marker();
}

void TimelineWidget::clampScroll() {
    const int content = count() * (kItem + kGap) + 40;
    const int room = std::max(0, width() - kLeft);
    m_scroll = std::clamp(m_scroll, 0, std::max(0, content - room));
}

QRect TimelineWidget::itemRect(int index) const {
    return QRect(kLeft + 12 + index * (kItem + kGap) - m_scroll, kTop + 1, kItem, kItem);
}

int TimelineWidget::boundaryX(int marker) const {
    if(marker <= 0) return itemRect(0).left() - kGap / 2 - 2;
    return itemRect(marker - 1).right() + kGap / 2 + 1;
}

int TimelineWidget::markerX() const { return boundaryX(shownMarker()); }

int TimelineWidget::nearestBoundary(int x) const {
    int best = 0, bestD = 1 << 30;
    for(int m = 0; m <= count(); ++m) {
        const int d = std::abs(boundaryX(m) - x);
        if(d < bestD) {
            bestD = d;
            best = m;
        }
    }
    return best;
}

int TimelineWidget::itemAt(QPoint p) const {
    for(int i = 0; i < count(); ++i)
        if(itemRect(i).adjusted(-2, -2, 2, 2).contains(p)) return i;
    return -1;
}

cad::Status TimelineWidget::statusOf(int index) const {
    if(!m_eval || index < 0 || index >= int(m_eval->statuses.size()) || index >= int(m_eval->features.size()))
        return cad::Status::ok();
    const auto &f = m_doc.features();
    if(index >= int(f.size()) || m_eval->features[size_t(index)]->id != f[size_t(index)]->id) return cad::Status::ok();
    return m_eval->statuses[size_t(index)];
}

void TimelineWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(QRect(kLeft, 0, width() - kLeft, height()));
    const auto &features = m_doc.features();
    const int marker = shownMarker();
    // The track.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(214, 219, 226));
    p.drawRoundedRect(QRectF(kLeft + 4, kTop - 2, width() - kLeft - 8, kItem + 6), 5, 5);
    for(int i = 0; i < int(features.size()); ++i) {
        const auto &f = features[size_t(i)];
        const QRect r = itemRect(i);
        const bool after = i >= marker;
        const cad::Status st = statusOf(i);
        QColor fill(250, 251, 253), border(150, 158, 170);
        if(st.isError()) fill = QColor(250, 205, 200), border = QColor(200, 60, 50);
        else if(st.severity == cad::Severity::Warning) fill = QColor(252, 236, 180), border = QColor(200, 150, 30);
        if(i == m_selected) border = QColor(30, 110, 220);
        if(i == m_hover) fill = fill.darker(106);
        p.setOpacity(after ? 0.4 : 1.0);
        p.setPen(QPen(border, i == m_selected ? 2.0 : 1.0));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        const QColor accent = f->suppressed ? QColor(150, 150, 150) : QColor(38, 110, 196);
        icon(iconFor(f->type()), accent).paint(&p, r.adjusted(4, 4, -4, -4));
        if(f->suppressed) {
            p.setPen(QPen(QColor(120, 120, 120), 1.6));
            p.drawLine(r.bottomLeft() + QPoint(4, -4), r.topRight() + QPoint(-4, 4));
        }
    }
    p.setOpacity(1.0);
    // The history marker: a bar with a grip.
    const int mx = markerX();
    const QColor mc = m_editing != cad::kNoFeature ? QColor(230, 140, 20) : QColor(40, 90, 170);
    p.setPen(Qt::NoPen);
    p.setBrush(mc);
    p.drawRoundedRect(QRectF(mx - 2, kTop - 4, 4, kItem + 10), 2, 2);
    QPolygonF grip;
    grip << QPointF(mx - 6, kTop - 5) << QPointF(mx + 6, kTop - 5) << QPointF(mx, kTop + 3);
    p.drawPolygon(grip);
}

void TimelineWidget::mousePressEvent(QMouseEvent *e) {
    if(e->button() != Qt::LeftButton || e->position().x() < kLeft) return QWidget::mousePressEvent(e);
    const QPoint pos = e->position().toPoint();
    if(std::abs(pos.x() - markerX()) <= 7 && m_editing == cad::kNoFeature) {
        m_dragging = true;
        m_dragStartMarker = m_doc.marker();
        m_dragSnapshot = m_doc.undoSnapshot();
        setCursor(Qt::SizeHorCursor);
        return;
    }
    m_selected = itemAt(pos);
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e) {
    const QPoint pos = e->position().toPoint();
    if(m_dragging) {
        const int m = nearestBoundary(pos.x());
        if(m != m_doc.marker()) m_doc.setMarker(m, false);
        return;
    }
    const int h = itemAt(pos);
    if(h != m_hover) {
        m_hover = h;
        update();
    }
    setCursor(std::abs(pos.x() - markerX()) <= 7 ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *e) {
    if(!m_dragging) return QWidget::mouseReleaseEvent(e);
    m_dragging = false;
    setCursor(Qt::ArrowCursor);
    if(m_doc.marker() != m_dragStartMarker) m_doc.pushUndoSnapshot("Move History Marker", std::move(m_dragSnapshot));
    m_dragSnapshot = cad::json();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    const int i = itemAt(e->position().toPoint());
    if(i >= 0) emit editRequested(m_doc.features()[size_t(i)]->id);
}

void TimelineWidget::wheelEvent(QWheelEvent *e) {
    m_scroll -= e->angleDelta().y() / 4 + e->angleDelta().x() / 4;
    clampScroll();
    update();
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent *e) {
    const int i = itemAt(e->pos());
    if(i < 0) return;
    m_selected = i;
    update();
    showMenu(i, e->globalPos());
}

void TimelineWidget::showMenu(int index, QPoint globalPos) {
    const cad::FeaturePtr f = m_doc.features()[size_t(index)];
    const cad::FeatureId id = f->id;
    QMenu menu(this);
    const bool sketch = f->type() == cad::FeatureType::Sketch;
    menu.addAction(icon(sketch ? IconId::Sketch : IconId::Display), sketch ? tr("Edit Sketch") : tr("Edit Feature"), this,
                   [this, id] { emit editRequested(id); });
    menu.addSeparator();
    menu.addAction(f->suppressed ? tr("Unsuppress Features") : tr("Suppress Features"), this,
                   [this, id, s = !f->suppressed] { m_doc.setSuppressed(id, s); });
    menu.addAction(tr("Roll History Marker Here"), this, [this, index] { m_doc.setMarker(index + 1); });
    menu.addAction(tr("Rename"), this, [this, id, name = QString::fromStdString(f->name)] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Rename"), tr("Name:"), QLineEdit::Normal, name, &ok);
        if(ok && !n.trimmed().isEmpty()) m_doc.renameFeature(id, n.trimmed().toStdString());
    });
    menu.addSeparator();
    menu.addAction(icon(IconId::Error), tr("Delete"), this, [this, id] { m_doc.deleteFeature(id); });
    menu.exec(globalPos);
}

bool TimelineWidget::event(QEvent *e) {
    if(e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int i = itemAt(he->pos());
        if(i >= 0) {
            const auto &f = m_doc.features()[size_t(i)];
            QString text = QString::fromStdString(f->name);
            if(f->suppressed) text += tr(" (suppressed)");
            const cad::Status st = statusOf(i);
            if(!st.isOk()) text += QStringLiteral("\n") + QString::fromStdString(st.message);
            QToolTip::showText(he->globalPos(), text, this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    if(e->type() == QEvent::Leave && m_hover >= 0) {
        m_hover = -1;
        update();
    }
    return QWidget::event(e);
}

} // namespace cadly
