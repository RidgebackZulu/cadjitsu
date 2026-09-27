#include "ui/TimelineWidget.h"

#include "features/PatternFeature.h"

#include "ui/Icons.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>

namespace cadly {

namespace {

constexpr int kItem = 30;       // feature icon size
constexpr int kGap = 6;         // space between icons
constexpr int kLeft = 142;      // playback buttons area
constexpr int kTop = 9;

IconId iconFor(cad::FeatureType t) {
    switch(t) {
    case cad::FeatureType::Sketch: return IconId::Sketch;
    case cad::FeatureType::Extrude: return IconId::Extrude;
    case cad::FeatureType::Fillet: return IconId::Fillet;
    case cad::FeatureType::Chamfer: return IconId::Chamfer;
    case cad::FeatureType::Hole: return IconId::Hole;
    case cad::FeatureType::Combine: return IconId::Combine;
    case cad::FeatureType::ConstructionPlane: return IconId::Plane;
    case cad::FeatureType::Split: return IconId::Split;
    case cad::FeatureType::Draft: return IconId::Draft;
    case cad::FeatureType::Pattern: return IconId::PatternRect;
    case cad::FeatureType::Thread: return IconId::Thread;
    }
    return IconId::Body;
}

IconId iconFor(const cad::Feature &f) {
    if(auto p = dynamic_cast<const cad::PatternFeature *>(&f))
        return p->kind == cad::PatternKind::Mirror     ? IconId::Mirror
               : p->kind == cad::PatternKind::Circular ? IconId::PatternCircular
                                                       : IconId::PatternRect;
    return iconFor(f.type());
}

QToolButton *playButton(QWidget *parent, IconId id, const QString &tip, const char *name) {
    auto *b = new QToolButton(parent);
    b->setObjectName(QString::fromLatin1(name));
    b->setIcon(icon(id));
    b->setIconSize(QSize(14, 14));
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
    setStyleSheet(QStringLiteral("#timeline { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f4f6f9,"
                                 " stop:1 #e8ecf1); border-top: 1px solid #c9ced6; }"
                                 "QToolButton { border: none; border-radius: 5px; padding: 3px; background: transparent; }"
                                 "QToolButton:hover { background: rgba(47, 123, 224, 38); }"
                                 "QToolButton:pressed { background: rgba(47, 123, 224, 75); }"));
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
    const qreal dpr = devicePixelRatioF();

    // The playback buttons sit in one rounded, segmented control.
    {
        const QRectF group(m_first->geometry().left() - 3.5, m_first->geometry().top() - 3.5,
                           m_last->geometry().right() - m_first->geometry().left() + 8, 31);
        QLinearGradient g(group.topLeft(), group.bottomLeft());
        g.setColorAt(0, QColor(255, 255, 255));
        g.setColorAt(1, QColor(226, 231, 238));
        p.setPen(QPen(QColor(178, 187, 200), 1));
        p.setBrush(g);
        p.drawRoundedRect(group, 7, 7);
        p.setPen(QPen(QColor(206, 213, 223), 1));
        for(QToolButton *b : {m_back, m_forward, m_last}) {
            const qreal x = b->geometry().left() - 2.5;
            p.drawLine(QPointF(x, group.top() + 6), QPointF(x, group.bottom() - 6));
        }
    }

    p.setClipRect(QRect(kLeft, 0, width() - kLeft, height()));
    const auto &features = m_doc.features();
    const int marker = shownMarker();

    // The track: a soft inset groove.
    const QRectF track(kLeft + 4, kTop - 3, width() - kLeft - 8, kItem + 8);
    QLinearGradient tg(track.topLeft(), track.bottomLeft());
    tg.setColorAt(0, QColor(203, 210, 220));
    tg.setColorAt(0.25, QColor(216, 222, 230));
    tg.setColorAt(1, QColor(228, 232, 238));
    p.setPen(QPen(QColor(186, 194, 206), 1));
    p.setBrush(tg);
    p.drawRoundedRect(track, 7, 7);

    for(int i = 0; i < int(features.size()); ++i) {
        const auto &f = features[size_t(i)];
        const QRect r = itemRect(i);
        const bool after = i >= marker;
        const bool selected = i == m_selected, hover = i == m_hover;
        const cad::Status st = statusOf(i);
        p.setOpacity(after ? 0.4 : 1.0);
        QRectF chip = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
        if(hover && !after) chip.translate(0, -1);
        // Shadow, lifted a little more on hover.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 40, 70, hover ? 60 : 34));
        p.drawRoundedRect(chip.translated(0, hover ? 2.0 : 1.2), 6, 6);
        if(selected) {
            p.setBrush(QColor(47, 123, 224, 70));
            p.drawRoundedRect(chip.adjusted(-2.5, -2.5, 2.5, 2.5), 8, 8);
        }
        QLinearGradient cg(chip.topLeft(), chip.bottomLeft());
        cg.setColorAt(0, hover ? QColor(255, 255, 255) : QColor(253, 254, 255));
        cg.setColorAt(1, hover ? QColor(226, 236, 248) : QColor(230, 235, 242));
        p.setPen(QPen(selected ? QColor(36, 110, 214) : QColor(160, 170, 184), selected ? 1.6 : 1.0));
        p.setBrush(cg);
        p.drawRoundedRect(chip, 6, 6);
        // Inner highlight along the top.
        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.drawLine(QPointF(chip.left() + 5, chip.top() + 1.2), QPointF(chip.right() - 5, chip.top() + 1.2));

        const QRect ir = chip.toAlignedRect().adjusted(4, 4, -4, -4);
        const QPixmap pm = icon(iconFor(*f))
                               .pixmap(ir.size(), dpr, f->suppressed ? QIcon::Disabled : QIcon::Normal);
        p.drawPixmap(ir, pm);

        // Status badges in the corner.
        auto badge = [&](const QColor &c, const QString &glyph) {
            const QRectF b(chip.right() - 10, chip.top() - 3, 13, 13);
            p.setPen(QPen(Qt::white, 1.4));
            p.setBrush(c);
            p.drawEllipse(b);
            QFont font = p.font();
            font.setPixelSize(10);
            font.setBold(true);
            p.setFont(font);
            p.setPen(Qt::white);
            p.drawText(b, Qt::AlignCenter, glyph);
        };
        if(f->suppressed) {
            p.setPen(QPen(QColor(120, 128, 140), 1.8, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(chip.left() + 5, chip.bottom() - 5), QPointF(chip.right() - 5, chip.top() + 5));
        }
        if(st.isError()) badge(QColor(214, 48, 38), QStringLiteral("!"));
        else if(st.severity == cad::Severity::Warning) badge(QColor(232, 160, 20), QStringLiteral("!"));
    }
    p.setOpacity(1.0);

    // The history marker: a slim bar with a rounded, gripped flag on top.
    const qreal mx = markerX() + 0.5;
    const bool editing = m_editing != cad::kNoFeature;
    const QColor top = editing ? QColor(255, 178, 70) : QColor(88, 156, 240);
    const QColor bottom = editing ? QColor(222, 118, 12) : QColor(28, 88, 184);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 40, 70, 50));
    p.drawRoundedRect(QRectF(mx - 1.5 + 1, kTop - 1, 3, kItem + 8), 1.5, 1.5);
    p.setBrush(bottom);
    p.drawRoundedRect(QRectF(mx - 1.5, kTop - 2, 3, kItem + 8), 1.5, 1.5);
    const QRectF flag(mx - 6, 1.5, 12, 11);
    QLinearGradient fg(flag.topLeft(), flag.bottomLeft());
    fg.setColorAt(0, top);
    fg.setColorAt(1, bottom);
    QPainterPath fp;
    fp.addRoundedRect(flag, 3.5, 3.5);
    QPolygonF tip;
    tip << QPointF(mx - 4, flag.bottom() - 1) << QPointF(mx + 4, flag.bottom() - 1) << QPointF(mx, flag.bottom() + 4);
    fp.addPolygon(tip);
    fp.setFillRule(Qt::WindingFill);
    p.setPen(QPen(bottom.darker(130), 1));
    p.setBrush(fg);
    p.drawPath(fp.simplified());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 210));
    for(int k = -1; k <= 1; ++k) p.drawEllipse(QPointF(mx + k * 3.0, flag.center().y() - 0.5), 1.0, 1.0);
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
    menu.addAction(icon(IconId::Delete), tr("Delete"), this, [this, id] { m_doc.deleteFeature(id); });
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
