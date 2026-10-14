#include "command/AlignCommand.h"

#include "model/ModelView.h"
#include "ui/AngleDial.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"
#include "viewport/ViewportTool.h"

#include "topo/Resolver.h"

#include <QCheckBox>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace cadjitsu {

namespace {
const QColor kBodies(240, 200, 120); // what moves
const QColor kFrom(230, 120, 20);    // orange
const QColor kTo(20, 100, 225);      // blue

QString colored(const QString &text, const QColor &c) {
    return QStringLiteral("<span style=\"color:%1; font-weight:600\">%2</span>").arg(c.name(), text);
}
} // namespace

// A dashed arrow on the canvas from the FROM anchor to the TO anchor, with
// their names: where the picked geometry is going.
class AlignArrow : public ViewportTool {
public:
    explicit AlignArrow(Viewport *vp) : m_vp(vp) {}
    std::optional<QVector3D> from, to;

    void paintOverlay(QPainter &p) override {
        if(!from && !to) return;
        p.setRenderHint(QPainter::Antialiasing);
        auto label = [&](QPointF at, const QString &text, const QColor &c) {
            QFont f = p.font();
            f.setBold(true);
            p.setFont(f);
            const QRectF box = QFontMetricsF(f).boundingRect(text).adjusted(-5, -2, 5, 2).translated(at + QPointF(10, -10));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 225));
            p.drawRoundedRect(box, 4, 4);
            p.setPen(c);
            p.drawText(box, Qt::AlignCenter, text);
        };
        auto dot = [&](QPointF at, const QColor &c) {
            p.setPen(QPen(Qt::white, 2));
            p.setBrush(c);
            p.drawEllipse(at, 5, 5);
        };
        const QPointF a = from ? m_vp->camera().project(*from) : QPointF(), b = to ? m_vp->camera().project(*to) : QPointF();
        if(from && to && QLineF(a, b).length() > 12) {
            QPen pen(QColor(60, 60, 70), 2, Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            const QLineF l(a, b);
            p.drawLine(l);
            // The head, at the TO end.
            const double ang = std::atan2(-l.dy(), l.dx());
            const QPointF tip = b;
            QPainterPath head;
            head.moveTo(tip);
            head.lineTo(tip - QPointF(std::cos(ang - 0.45) * 13, -std::sin(ang - 0.45) * 13));
            head.lineTo(tip - QPointF(std::cos(ang + 0.45) * 13, -std::sin(ang + 0.45) * 13));
            head.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(60, 60, 70));
            p.drawPath(head);
        }
        if(from) {
            dot(a, kFrom);
            label(a, QObject::tr("FROM"), kFrom);
        }
        if(to) {
            dot(b, kTo);
            label(b, QObject::tr("TO"), kTo);
        }
    }

private:
    Viewport *m_vp;
};

AlignCommand::AlignCommand(const CommandContext &ctx, cad::FeatureId editing) : Command(ctx, editing) {}
AlignCommand::~AlignCommand() = default;

QString AlignCommand::prompt() const {
    return tr("Align: click the body to move, then a point, edge or face on it (FROM), then where it goes (TO).");
}

IconId AlignCommand::iconId() const { return IconId::Align; }

ViewportTool *AlignCommand::tool() {
    if(!m_arrow) m_arrow = std::make_unique<AlignArrow>(m_ctx.viewport);
    return m_arrow.get();
}

void AlignCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::AlignFeature>(doc.feature(m_editing));
    const cad::AlignFeature *o = m_original.get();
    m_guide = panel.addInfo(QString(), "alignGuide");
    m_guide->setWordWrap(true);
    m_guide->setTextFormat(Qt::RichText);
    m_guide->setMinimumWidth(240);
    m_guide->setStyleSheet(QStringLiteral("font-weight: normal; color: #2a3140; background: #eef3fb; border: 1px solid "
                                          "#c9d6ec; border-radius: 4px; padding: 4px 6px;"));
    m_objectsField = panel.addSelection(tr("Bodies"), tr("Select the bodies to move"), "alignBodies");
    m_fromField = panel.addSelection(tr("From"), tr("A point, edge or face on them"), "alignFrom");
    m_toField = panel.addSelection(tr("To"), tr("Where it goes"), "alignTo");
    m_flip = panel.addCheck(tr("Flip"), "alignFlip");
    m_flip->setToolTip(tr("Turn it the other way round: faces stacked the same way instead of meeting, an edge end "
                          "for end."));
    m_angle = panel.addAngle(tr("Angle"), evaluator(), "alignAngle", kTo);
    panel.angleDial(m_angle)->setFullTurn(true);
    m_angle->setToolTip(tr("Spin it about the line or face normal it was aligned on."));
    m_copy = panel.addCheck(tr("Copy"), "alignCopy");
    m_copy->setToolTip(tr("Leave the bodies where they are and align copies of them."));
    m_angleSlot = o && !o->angle.empty() ? o->angle : doc.makeSlot("0 deg");
    m_angle->setExpression(QString::fromStdString(m_angleSlot.expr));
    if(o) {
        m_bodies = o->bodies;
        m_from = o->from;
        m_to = o->to;
        m_flip->setChecked(o->flip);
        m_copy->setChecked(o->copy);
    } else {
        for(const auto &it : m_ctx.view->selection().items())
            if(it.kind == SelectionItem::Kind::Body &&
               std::find(m_bodies.begin(), m_bodies.end(), it.body) == m_bodies.end())
                m_bodies.push_back(it.body);
    }
    connect(m_angle, &ValueField::edited, this, &Command::inputsChanged);
    for(QCheckBox *c : {m_flip, m_copy})
        connect(c, &QCheckBox::toggled, this, [this] {
            updateGuide();
            emit inputsChanged();
        });
    const std::pair<SelectionField *, Field> fields[] = {
        {m_objectsField, Field::Objects}, {m_fromField, Field::From}, {m_toField, Field::To}};
    for(const auto &[sf, f] : fields) {
        const Field fld = f;
        connect(sf, &SelectionField::activated, this, [this, fld] { activate(fld); });
        connect(sf, &SelectionField::cleared, this, [this, fld] {
            if(fld == Field::Objects) {
                m_bodies.clear();
                m_from = {};
            } else if(fld == Field::From) {
                m_from = {};
            } else {
                m_to = {};
            }
            activate(fld);
            emit inputsChanged();
        });
    }
    activate(m_bodies.empty() ? Field::Objects : m_from.empty() ? Field::From : m_to.empty() ? Field::To : Field::Objects);
}

bool AlignCommand::onMovedBody(const cad::BodyId &b) const {
    return std::find(m_bodies.begin(), m_bodies.end(), b) != m_bodies.end();
}

void AlignCommand::activate(Field f) {
    m_active = f;
    m_objectsField->setActive(f == Field::Objects);
    m_fromField->setActive(f == Field::From);
    m_toField->setActive(f == Field::To);
    SelectFilter sf;
    sf.profiles = false;
    sf.bodies = false;
    if(f == Field::Objects) {
        sf.edges = sf.vertices = false;
        sf.faceSelectsBody = true;
    } else if(f == Field::To) {
        sf.planes = true; // the construction and origin planes on show too
    }
    m_ctx.view->setFilter(sf);
    updateMarks();
}

void AlignCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState()) {
        for(const cad::BodyId &b : m_bodies) markInput(*m_ctx.view, *base, InputRef::ofBody(b), -1, kBodies, marks);
        auto mark = [&](const cad::AlignRef &r, const QColor &c) {
            if(!r.ref.empty()) markInput(*m_ctx.view, *base, InputRef::ofTopo(r.ref), -1, c, marks);
            else if(r.plane) markInput(*m_ctx.view, *base, InputRef::ofPlane(*r.plane), -1, c, marks);
        };
        mark(m_from, kFrom);
        mark(m_to, kTo);
    }
    m_ctx.view->setInputMarks(std::move(marks));
    m_objectsField->setCount(int(m_bodies.size()));
    m_fromField->setCount(m_from.empty() ? 0 : 1);
    m_toField->setCount(m_to.empty() ? 0 : 1);
    updateGuide();
}

void AlignCommand::updateGuide() {
    // Step by step: what to click next; once both ends are picked, what the
    // pair will do (or why it cannot).
    QString text;
    std::optional<cad::AlignAnchor> a, b;
    QString problem;
    if(const cad::StatePtr base = baseState()) {
        cad::Status st;
        if(!m_from.empty()) {
            a = cad::anchorOf(*base, m_from, st);
            if(!a) problem = QString::fromStdString(st.message);
        }
        cad::Status st2;
        if(!m_to.empty()) {
            b = cad::anchorOf(*base, m_to, st2);
            if(!b) problem = QString::fromStdString(st2.message);
        }
    }
    const QString step = tr("<b>Step %1 of 3</b> — ");
    if(m_bodies.empty()) {
        text = step.arg(1) + tr("Click the body (or bodies) you want to move.");
    } else if(m_from.empty()) {
        text = step.arg(2) + tr("Click a point, an edge or a face %1 on it: the part of it that will be lined up.")
                                 .arg(colored(tr("FROM"), kFrom));
    } else if(m_to.empty()) {
        text = step.arg(3) + tr("Click where it goes %1: a point, an edge, a face or a plane on something else.")
                                 .arg(colored(tr("TO"), kTo));
    } else if(!problem.isEmpty()) {
        text = tr("<b>Not that one:</b> %1.").arg(problem.toHtmlEscaped());
    } else if(a && b) {
        std::string why;
        if(cad::alignTransform(*a, *b, m_flip->isChecked(), 0, why)) {
            auto name = [&](cad::AlignKind k) {
                const QString n = tr(cad::alignKindName(k));
                return n.left(1).toUpper() + n.mid(1);
            };
            text = tr("<b>%1 → %2:</b> %3")
                       .arg(colored(name(a->kind), kFrom), colored(tr(cad::alignKindName(b->kind)), kTo),
                            QString::fromStdString(cad::describeAlign(a->kind, b->kind)).toHtmlEscaped());
            text += tr("<br>Click OK to keep it, or click a field to pick again.");
        } else {
            text = tr("<b>Can't do that pair:</b> %1.").arg(QString::fromStdString(why).toHtmlEscaped());
        }
    }
    m_guide->setText(text);
    // Tall enough for the whole text at the panel's width.
    m_guide->setMinimumHeight(m_guide->heightForWidth(std::max(m_guide->width(), m_guide->minimumWidth())));
    m_ctx.panel->adjustSize();
    if(m_arrow) {
        auto v = [](const gp_Pnt &p) { return QVector3D(float(p.X()), float(p.Y()), float(p.Z())); };
        m_arrow->from = a ? std::optional<QVector3D>(v(a->p)) : std::nullopt;
        m_arrow->to = b ? std::optional<QVector3D>(v(b->p)) : std::nullopt;
        m_ctx.viewport->update();
    }
}

void AlignCommand::picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers) {
    if(!item) return;
    const gp_Pnt at(hit.point.x(), hit.point.y(), hit.point.z());
    switch(m_active) {
    case Field::Objects: {
        if(item->kind != SelectionItem::Kind::Body) return;
        auto it = std::find(m_bodies.begin(), m_bodies.end(), item->body);
        if(it != m_bodies.end()) m_bodies.erase(it);
        else m_bodies.push_back(item->body);
        // FROM must stay on what moves.
        if(!m_from.empty())
            if(const cad::StatePtr base = baseState()) {
                const cad::ResolvedRef r = cad::resolveRef(*base, m_from.ref);
                if(!r.ok || !r.body || !onMovedBody(r.body->id)) m_from = {};
            }
        if(!m_bodies.empty() && m_from.empty()) {
            activate(Field::From); // FROM next
            emit inputsChanged();
            return;
        }
        break;
    }
    case Field::From: {
        if(item->kind != SelectionItem::Kind::Face && item->kind != SelectionItem::Kind::Edge &&
           item->kind != SelectionItem::Kind::Vertex)
            return;
        if(!onMovedBody(item->body)) {
            m_ctx.panel->setMessage(tr("FROM goes on the body being moved (the highlighted one). To move this body, "
                                       "click Bodies and pick it there."),
                                    cad::Severity::Warning);
            return;
        }
        const auto r = inputRefOf(*m_ctx.view, *item);
        if(!r) return;
        m_from = {};
        m_from.ref = r->topo;
        m_from.at = at;
        if(m_to.empty()) {
            activate(Field::To); // TO next
            emit inputsChanged();
            return;
        }
        break;
    }
    case Field::To: {
        if(item->kind == SelectionItem::Kind::Plane) {
            const auto p = m_ctx.view->planeRefOf(*item);
            if(!p) return;
            m_to = {};
            if(p->kind == cad::PlaneRef::Kind::Face) m_to.ref = p->face;
            else m_to.plane = *p;
        } else {
            if(item->kind != SelectionItem::Kind::Face && item->kind != SelectionItem::Kind::Edge &&
               item->kind != SelectionItem::Kind::Vertex)
                return;
            if(onMovedBody(item->body)) {
                m_ctx.panel->setMessage(tr("TO is where it goes: pick something that stays put, not on the body being "
                                           "moved."),
                                        cad::Severity::Warning);
                return;
            }
            const auto r = inputRefOf(*m_ctx.view, *item);
            if(!r) return;
            m_to = {};
            m_to.ref = r->topo;
        }
        m_to.at = at;
        break;
    }
    }
    updateMarks();
    emit inputsChanged();
}

std::shared_ptr<cad::Feature> AlignCommand::build(QString &why) {
    // The guide says what is missing; the message line stays for problems.
    if(m_bodies.empty() || m_from.empty() || m_to.empty()) return nullptr;
    if(!m_angle->valid()) {
        why = tr("Enter the angle.");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::AlignFeature>(m_original->clone()) : std::make_shared<cad::AlignFeature>();
    f->bodies = m_bodies;
    f->from = m_from;
    f->to = m_to;
    f->flip = m_flip->isChecked();
    f->copy = m_copy->isChecked();
    f->angle = {m_angleSlot.name, m_angle->expression().toStdString()};
    return f;
}

} // namespace cadjitsu
