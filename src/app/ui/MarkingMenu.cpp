#include "ui/MarkingMenu.h"

#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include <cmath>

namespace cadly {

namespace {

constexpr int kRadius = 118;
constexpr int kSlotW = 150, kSlotH = 30;
constexpr int kDead = 26; // no slot inside this radius
constexpr int kRowH = 26, kListW = 210;

} // namespace

MarkingMenu::MarkingMenu(QWidget *canvas) : QWidget(canvas) {
    setObjectName(QStringLiteral("markingMenu"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    hide();
}

void MarkingMenu::setRing(const std::vector<QAction *> &actions) {
    m_ring.assign(8, nullptr);
    for(size_t i = 0; i < actions.size() && i < 8; ++i) m_ring[i] = actions[i];
}

void MarkingMenu::setList(const std::vector<QAction *> &actions) {
    m_list.clear();
    for(QAction *a : actions) m_list.push_back(a);
}

void MarkingMenu::open(QPoint canvasPos) {
    QWidget *canvas = parentWidget();
    setGeometry(canvas->rect());
    // Keep the ring on screen.
    const int mx = kRadius + kSlotW / 2 + 4, my = kRadius + kSlotH;
    const int listH = int(m_list.size()) * kRowH + 12;
    m_center = QPoint(std::clamp(canvasPos.x(), std::min(mx, width() / 2), std::max(width() - mx, width() / 2)),
                      std::clamp(canvasPos.y(), std::min(my, height() / 2),
                                 std::max(height() - my - listH, height() / 2)));
    m_hotSlot = m_hotRow = -1;
    show();
    raise();
    setFocus(Qt::PopupFocusReason);
}

void MarkingMenu::close() {
    hide();
    if(parentWidget()) parentWidget()->setFocus(Qt::OtherFocusReason);
}

QPoint MarkingMenu::slotCenter(int slot) const {
    const double a = slot * M_PI / 4; // clockwise from north
    return m_center + QPoint(int(std::round(std::sin(a) * kRadius)), int(std::round(-std::cos(a) * kRadius * 0.82)));
}

QRect MarkingMenu::slotRect(int slot) const {
    const QPoint c = slotCenter(slot);
    return QRect(c.x() - kSlotW / 2, c.y() - kSlotH / 2, kSlotW, kSlotH);
}

QRect MarkingMenu::listRow(int row) const {
    const int top = m_center.y() + int(kRadius * 0.82) + kSlotH / 2 + 18;
    return QRect(m_center.x() - kListW / 2, top + 6 + row * kRowH, kListW, kRowH);
}

int MarkingMenu::slotAt(QPoint p) const {
    for(int i = 0; i < 8; ++i)
        if(m_ring[size_t(i)] && slotRect(i).contains(p)) return i;
    const QPoint d = p - m_center;
    const double r = std::hypot(d.x(), d.y());
    if(r < kDead || r > kRadius + kSlotW / 2.0) return -1;
    if(rowAt(p) >= 0) return -1;
    // The direction picks the slot, like flicking in Fusion.
    double a = std::atan2(double(d.x()), double(-d.y()));
    if(a < 0) a += 2 * M_PI;
    const int slot = int(std::round(a / (M_PI / 4))) % 8;
    return m_ring[size_t(slot)] ? slot : -1;
}

int MarkingMenu::rowAt(QPoint p) const {
    for(int i = 0; i < int(m_list.size()); ++i)
        if(m_list[size_t(i)] && listRow(i).contains(p)) return i;
    return -1;
}

void MarkingMenu::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    // Centre.
    p.setPen(QPen(QColor(90, 100, 115, 160), 1.5));
    p.setBrush(QColor(255, 255, 255, 170));
    p.drawEllipse(m_center, 14, 14);
    if(m_hotSlot >= 0) {
        p.setPen(QPen(QColor(30, 110, 220), 2.0));
        p.drawLine(m_center, slotCenter(m_hotSlot));
    }
    for(int i = 0; i < 8; ++i) {
        QAction *a = m_ring[size_t(i)];
        if(!a) continue;
        const QRect r = slotRect(i);
        const bool hot = i == m_hotSlot, enabled = a->isEnabled();
        p.setPen(QPen(hot ? QColor(30, 110, 220) : QColor(140, 150, 165), 1.0));
        p.setBrush(hot ? QColor(222, 236, 255, 250) : QColor(250, 251, 253, 240));
        p.drawRoundedRect(r, 5, 5);
        if(!a->icon().isNull())
            a->icon().paint(&p, QRect(r.left() + 6, r.top() + 5, 20, 20), Qt::AlignCenter,
                            enabled ? QIcon::Normal : QIcon::Disabled);
        p.setPen(enabled ? QColor(30, 36, 44) : QColor(160, 166, 175));
        p.drawText(r.adjusted(30, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(a->text().remove(QLatin1Char('&')), Qt::ElideRight, r.width() - 36));
    }
    if(!m_list.empty()) {
        const QRect box = listRow(0).united(listRow(int(m_list.size()) - 1)).adjusted(-4, -6, 4, 6);
        p.setPen(QPen(QColor(140, 150, 165), 1.0));
        p.setBrush(QColor(250, 251, 253, 245));
        p.drawRoundedRect(box, 5, 5);
        for(int i = 0; i < int(m_list.size()); ++i) {
            QAction *a = m_list[size_t(i)];
            if(!a) continue;
            const QRect r = listRow(i);
            if(a->isSeparator()) {
                p.setPen(QColor(210, 214, 220));
                p.drawLine(r.left() + 6, r.center().y(), r.right() - 6, r.center().y());
                continue;
            }
            if(i == m_hotRow && a->isEnabled()) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(222, 236, 255));
                p.drawRoundedRect(r.adjusted(0, 1, 0, -1), 3, 3);
            }
            if(!a->icon().isNull())
                a->icon().paint(&p, QRect(r.left() + 6, r.top() + 4, 18, 18), Qt::AlignCenter,
                                a->isEnabled() ? QIcon::Normal : QIcon::Disabled);
            p.setPen(a->isEnabled() ? QColor(30, 36, 44) : QColor(160, 166, 175));
            p.drawText(r.adjusted(30, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, a->text().remove(QLatin1Char('&')));
        }
    }
}

void MarkingMenu::mouseMoveEvent(QMouseEvent *e) {
    const QPoint p = e->position().toPoint();
    const int s = slotAt(p), r = rowAt(p);
    if(s != m_hotSlot || r != m_hotRow) {
        m_hotSlot = s;
        m_hotRow = r;
        update();
    }
}

void MarkingMenu::mousePressEvent(QMouseEvent *e) {
    // A press outside the ring and the list closes the menu.
    const QPoint p = e->position().toPoint();
    if(slotAt(p) < 0 && rowAt(p) < 0) close();
}

void MarkingMenu::mouseReleaseEvent(QMouseEvent *e) {
    const QPoint p = e->position().toPoint();
    const int s = slotAt(p), r = rowAt(p);
    if(s >= 0) trigger(m_ring[size_t(s)]);
    else if(r >= 0) trigger(m_list[size_t(r)]);
}

void MarkingMenu::keyPressEvent(QKeyEvent *e) {
    if(e->key() == Qt::Key_Escape) close();
    else QWidget::keyPressEvent(e);
}

void MarkingMenu::trigger(QAction *a) {
    if(!a || !a->isEnabled() || a->isSeparator()) return;
    close();
    // After the menu is gone (the action may open dialogs or change the canvas).
    QPointer<QAction> guard(a);
    QTimer::singleShot(0, this, [guard] {
        if(guard) guard->trigger();
    });
}

} // namespace cadly
