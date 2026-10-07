#include "command/ShellCommand.h"

#include "model/ModelView.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include <QComboBox>

#include <algorithm>

namespace cadjitsu {

namespace {
const QColor kFace(230, 140, 40);
const QColor kBody(20, 100, 225);
constexpr int kBodyTag = 1000;
} // namespace

ShellCommand::ShellCommand(const CommandContext &ctx, cad::FeatureId editing) : Command(ctx, editing) {}

IconId ShellCommand::iconId() const { return IconId::Shell; }

void ShellCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::ShellFeature>(doc.feature(m_editing));
    m_facesField = panel.addSelection(tr("Faces"), tr("Select faces to remove"), "shellFaces");
    m_bodiesField = panel.addSelection(tr("Bodies"), tr("Or a body to hollow"), "shellBodies");
    m_thicknessField = panel.addValue(tr("Thickness"), cad::ValueKind::Length, evaluator(), "shellThickness");
    m_direction = panel.addChoice(tr("Direction"), {tr("Inside"), tr("Outside")}, "shellDirection");
    m_direction->setToolTip(tr("Inside keeps the outer size; Outside keeps the inner size (walls grow out)."));
    m_thickness = m_original && !m_original->thickness.empty() ? m_original->thickness : doc.makeSlot("2 mm");
    m_thicknessField->setExpression(QString::fromStdString(m_thickness.expr));
    if(m_original) {
        for(const auto &f : m_original->faces) m_faces.push_back(InputRef::ofTopo(f));
        m_bodies = m_original->bodies;
        m_direction->setCurrentIndex(m_original->direction == cad::ShellDirection::Outside ? 1 : 0);
    } else {
        for(const auto &it : m_ctx.view->selection().items()) {
            if(it.kind == SelectionItem::Kind::Face) {
                if(auto r = inputRefOf(*m_ctx.view, it)) m_faces.push_back(*r);
            } else if(it.kind == SelectionItem::Kind::Body) {
                m_bodies.push_back(it.body);
            }
        }
    }
    connect(m_thicknessField, &ValueField::edited, this, &ShellCommand::inputsChanged);
    connect(m_direction, &QComboBox::activated, this, &ShellCommand::inputsChanged);
    connect(m_facesField, &SelectionField::activated, this, [this] { activate(Field::Faces); });
    connect(m_bodiesField, &SelectionField::activated, this, [this] { activate(Field::Bodies); });
    connect(m_facesField, &SelectionField::cleared, this, [this] {
        m_faces.clear();
        activate(Field::Faces);
        emit inputsChanged();
    });
    connect(m_bodiesField, &SelectionField::cleared, this, [this] {
        m_bodies.clear();
        activate(Field::Bodies);
        emit inputsChanged();
    });
    activate(Field::Faces);
    m_thicknessField->setFocus(Qt::OtherFocusReason);
    m_thicknessField->selectAll();
}

void ShellCommand::activate(Field f) {
    m_active = f;
    m_facesField->setActive(f == Field::Faces);
    m_bodiesField->setActive(f == Field::Bodies);
    SelectFilter sf;
    sf.vertices = sf.profiles = sf.edges = false;
    sf.faceSelectsBody = f == Field::Bodies;
    m_ctx.view->setFilter(sf);
    updateMarks();
}

void ShellCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState()) {
        for(size_t i = 0; i < m_faces.size(); ++i) markInput(*m_ctx.view, *base, m_faces[i], int(i), kFace, marks);
        for(size_t i = 0; i < m_bodies.size(); ++i)
            markInput(*m_ctx.view, *base, InputRef::ofBody(m_bodies[i]), kBodyTag + int(i), kBody, marks);
    }
    m_ctx.view->setInputMarks(std::move(marks));
    m_facesField->setCount(int(m_faces.size()));
    m_bodiesField->setCount(int(m_bodies.size()));
}

void ShellCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item) return;
    if(m_active == Field::Faces) {
        if(item->kind != SelectionItem::Kind::Face) return;
        const std::optional<InputRef> r = inputRefOf(*m_ctx.view, *item);
        if(!r || r->createdBy(isEditing() ? m_editing : m_ctx.doc->nextFeatureId())) return;
        toggleRef(m_faces, *r);
    } else {
        if(item->kind != SelectionItem::Kind::Body && item->kind != SelectionItem::Kind::Face) return;
        const cad::BodyId id = item->body;
        auto at = std::find(m_bodies.begin(), m_bodies.end(), id);
        if(at != m_bodies.end()) m_bodies.erase(at);
        else m_bodies.push_back(id);
    }
    updateMarks();
    emit inputsChanged();
}

void ShellCommand::markClicked(int tag) {
    if(tag >= kBodyTag && tag - kBodyTag < int(m_bodies.size())) m_bodies.erase(m_bodies.begin() + (tag - kBodyTag));
    else if(tag >= 0 && tag < int(m_faces.size())) m_faces.erase(m_faces.begin() + tag);
    updateMarks();
    emit inputsChanged();
}

std::shared_ptr<cad::Feature> ShellCommand::build(QString &why) {
    if(m_faces.empty() && m_bodies.empty()) {
        why = tr("Select the faces to remove (or a body to hollow).");
        return nullptr;
    }
    if(!m_thicknessField->valid()) {
        why = tr("Thickness: enter a value");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::ShellFeature>(m_original->clone())
                        : std::make_shared<cad::ShellFeature>();
    f->faces.clear();
    for(const auto &r : m_faces) f->faces.push_back(r.topo);
    // A body whose faces are picked is shelled through them, not sealed too.
    f->bodies.clear();
    for(const auto &b : m_bodies) f->bodies.push_back(b);
    f->thickness = {m_thickness.name, m_thicknessField->expression().toStdString()};
    f->direction = m_direction->currentIndex() == 1 ? cad::ShellDirection::Outside : cad::ShellDirection::Inside;
    return f;
}

} // namespace cadjitsu
