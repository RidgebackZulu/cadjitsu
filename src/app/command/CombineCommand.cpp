#include "command/CombineCommand.h"

#include "model/ModelView.h"
#include "ui/Icons.h"

#include <QCheckBox>
#include <QComboBox>

#include <algorithm>

namespace cadly {

namespace {

const QColor kTarget(20, 100, 225);
const QColor kTool(230, 140, 40);
const cad::BodyOperation kOperations[] = {cad::BodyOperation::Join, cad::BodyOperation::Cut,
                                          cad::BodyOperation::Intersect};

} // namespace

IconId CombineCommand::iconId() const { return IconId::Combine; }

void CombineCommand::setup() {
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::CombineFeature>(m_ctx.doc->feature(m_editing));
    m_targetField = panel.addSelection(tr("Target Body"), tr("Select a body"), "combineTarget");
    m_toolsField = panel.addSelection(tr("Tool Bodies"), tr("Select bodies"), "combineTools");
    m_operation = panel.addChoice(tr("Operation"), {tr("Join"), tr("Cut"), tr("Intersect")}, "combineOperation");
    m_keep = panel.addCheck(tr("Keep Tools"), "combineKeepTools");
    if(m_original) {
        m_target = {InputRef::ofBody(m_original->target)};
        for(const auto &b : m_original->tools) m_tools.push_back(InputRef::ofBody(b));
        for(int i = 0; i < 3; ++i)
            if(kOperations[i] == m_original->operation) m_operation->setCurrentIndex(i);
        m_keep->setChecked(m_original->keepTools);
    } else {
        // Bodies selected beforehand: the first is the target, as in Fusion.
        for(const auto &it : m_ctx.view->selection().items())
            if(it.kind == SelectionItem::Kind::Body) {
                if(m_target.empty()) m_target = {InputRef::ofBody(it.body)};
                else m_tools.push_back(InputRef::ofBody(it.body));
            }
    }
    SelectFilter sf;
    sf.profiles = sf.bodies = false;
    sf.faceSelectsBody = true;
    m_ctx.view->setFilter(sf);
    connect(m_targetField, &SelectionField::activated, this, [this] { activateTools(false); });
    connect(m_toolsField, &SelectionField::activated, this, [this] { activateTools(true); });
    connect(m_targetField, &SelectionField::cleared, this, [this] {
        m_target.clear();
        activateTools(false);
        emit inputsChanged();
    });
    connect(m_toolsField, &SelectionField::cleared, this, [this] {
        m_tools.clear();
        activateTools(true);
        emit inputsChanged();
    });
    connect(m_operation, &QComboBox::currentIndexChanged, this, &Command::inputsChanged);
    connect(m_keep, &QCheckBox::toggled, this, &Command::inputsChanged);
    activateTools(!m_target.empty());
}

void CombineCommand::activateTools(bool tools) {
    m_toolsActive = tools;
    m_targetField->setActive(!tools);
    m_toolsField->setActive(tools);
    updateMarks();
}

void CombineCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState()) {
        for(const auto &t : m_target) markInput(*m_ctx.view, *base, t, -1, kTarget, marks);
        for(size_t i = 0; i < m_tools.size(); ++i) markInput(*m_ctx.view, *base, m_tools[i], int(i), kTool, marks);
    }
    m_ctx.view->setInputMarks(std::move(marks));
    m_targetField->setCount(int(m_target.size()));
    m_toolsField->setCount(int(m_tools.size()));
}

void CombineCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item || item->kind != SelectionItem::Kind::Body) return;
    const InputRef r = InputRef::ofBody(item->body);
    if(!m_toolsActive) {
        m_target = {r};
        m_tools.erase(std::remove(m_tools.begin(), m_tools.end(), r), m_tools.end());
        activateTools(true); // the tools come next
    } else {
        if(!m_target.empty() && m_target.front() == r) return;
        toggleRef(m_tools, r);
        updateMarks();
    }
    emit inputsChanged();
}

void CombineCommand::markClicked(int tag) {
    if(tag < 0 || tag >= int(m_tools.size())) return;
    m_tools.erase(m_tools.begin() + tag);
    updateMarks();
    emit inputsChanged();
}

std::shared_ptr<cad::Feature> CombineCommand::build(QString &why) {
    if(m_target.empty()) {
        why = tr("Select the target body.");
        return nullptr;
    }
    if(m_tools.empty()) {
        why = tr("Select the tool bodies.");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::CombineFeature>(m_original->clone())
                        : std::make_shared<cad::CombineFeature>();
    f->target = m_target.front().body;
    f->tools.clear();
    for(const auto &t : m_tools) f->tools.push_back(t.body);
    f->operation = kOperations[std::clamp(m_operation->currentIndex(), 0, 2)];
    f->keepTools = m_keep->isChecked();
    return f;
}

} // namespace cadly
