#include "ui/BrowserTree.h"

#include "model/ModelView.h"
#include "ui/Icons.h"

#include <QHeaderView>

#include <set>

namespace cadly {

BrowserTree::BrowserTree(cad::Document &doc, ModelView *view, QWidget *parent)
    : QTreeWidget(parent), m_doc(doc), m_view(view) {
    setObjectName(QStringLiteral("browser"));
    setColumnCount(2);
    setHeaderHidden(true);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::Fixed);
    header()->resizeSection(1, 26);
    setIconSize(QSize(16, 16));
    setIndentation(14);
    setFocusPolicy(Qt::ClickFocus);
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    setStyleSheet(QStringLiteral("#browser { background: rgba(250, 251, 253, 240); border: none;"
                                 " border-right: 1px solid #c9ced6; font-size: 12px; }"));
    connect(this, &QTreeWidget::itemClicked, this, &BrowserTree::onClicked);
    connect(this, &QTreeWidget::itemDoubleClicked, this, &BrowserTree::onDoubleClicked);
    connect(this, &QTreeWidget::itemChanged, this, &BrowserTree::onChanged);
    rebuild();
}

void BrowserTree::setDocumentName(const QString &name) {
    m_name = name;
    rebuild();
}

QTreeWidgetItem *BrowserTree::folder(const QString &name) const {
    const auto found = findItems(name, Qt::MatchExactly | Qt::MatchRecursive, 0);
    return found.isEmpty() ? nullptr : found.front();
}

void BrowserTree::setEye(QTreeWidgetItem *item, bool visible) {
    item->setIcon(1, icon(visible ? IconId::Eye : IconId::EyeOff));
    item->setToolTip(1, visible ? tr("Hide") : tr("Show"));
}

void BrowserTree::rebuild() {
    m_rebuilding = true;
    // Remember which folders were open.
    std::set<int> collapsed;
    for(int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *root = topLevelItem(i);
        for(int k = 0; k < root->childCount(); ++k)
            if(!root->child(k)->isExpanded()) collapsed.insert(root->child(k)->data(0, KindRole).toInt());
    }
    const bool firstBuild = topLevelItemCount() == 0;
    clear();
    auto *root = new QTreeWidgetItem(this, {m_name});
    root->setData(0, KindRole, Root);
    root->setIcon(0, icon(IconId::Body));
    QFont bold = root->font(0);
    bold.setBold(true);
    root->setFont(0, bold);

    auto makeFolder = [&](const QString &name, Kind kind) {
        auto *f = new QTreeWidgetItem(root, {name});
        f->setData(0, KindRole, kind);
        f->setIcon(0, icon(IconId::Folder));
        return f;
    };
    QTreeWidgetItem *origin = makeFolder(tr("Origin"), OriginFolder);
    origin->setIcon(0, icon(IconId::Origin));
    setEye(origin, m_view->originVisible());

    const cad::StatePtr st = m_view->state();
    QTreeWidgetItem *bodies = makeFolder(tr("Bodies"), BodiesFolder);
    QTreeWidgetItem *sketches = makeFolder(tr("Sketches"), SketchesFolder);
    QTreeWidgetItem *construction = makeFolder(tr("Construction"), ConstructionFolder);
    if(st) {
        for(const cad::Body *b : st->orderedBodies()) {
            auto *it = new QTreeWidgetItem(bodies, {QString::fromStdString(m_doc.bodyName(*b))});
            it->setData(0, KindRole, BodyItem);
            it->setData(0, IdRole, QString::fromStdString(b->id));
            it->setIcon(0, icon(IconId::Body));
            it->setFlags(it->flags() | Qt::ItemIsEditable);
            setEye(it, m_doc.bodyVisible(b->id));
        }
        for(const auto &[fid, sk] : st->sketches) {
            auto *it = new QTreeWidgetItem(sketches, {QString::fromStdString(sk->name)});
            it->setData(0, KindRole, SketchItem);
            it->setData(0, IdRole, fid);
            it->setIcon(0, icon(IconId::SketchNode));
            if(!sk->status.isOk()) {
                it->setIcon(0, icon(sk->status.isError() ? IconId::Error : IconId::Warning));
                it->setToolTip(0, QString::fromStdString(sk->status.message));
            }
            setEye(it, m_view->sketchShown(fid));
        }
        for(const auto &[fid, plane] : st->planes) {
            auto *it = new QTreeWidgetItem(construction, {QString::fromStdString(plane->name)});
            it->setData(0, KindRole, PlaneItem);
            it->setData(0, IdRole, fid);
            it->setIcon(0, icon(IconId::PlaneNode));
        }
    }
    root->setExpanded(true);
    for(QTreeWidgetItem *f : {origin, bodies, sketches, construction}) {
        const Kind k = Kind(f->data(0, KindRole).toInt());
        f->setExpanded(firstBuild ? k != OriginFolder && k != ConstructionFolder : !collapsed.count(k));
        f->setHidden(f->childCount() == 0 && k != OriginFolder && k != BodiesFolder);
    }
    m_rebuilding = false;
}

void BrowserTree::onClicked(QTreeWidgetItem *item, int column) {
    const Kind kind = Kind(item->data(0, KindRole).toInt());
    if(column == 1) {
        switch(kind) {
        case OriginFolder:
            m_view->setOriginVisible(!m_view->originVisible());
            break;
        case BodyItem: {
            const cad::BodyId id = item->data(0, IdRole).toString().toStdString();
            m_doc.setBodyVisible(id, !m_doc.bodyVisible(id));
            break;
        }
        case SketchItem: {
            const cad::FeatureId id = item->data(0, IdRole).toInt();
            m_doc.setSketchVisible(id, !m_view->sketchShown(id));
            break;
        }
        default:
            return;
        }
        // Not from inside the click on the item being replaced.
        QMetaObject::invokeMethod(this, &BrowserTree::rebuild, Qt::QueuedConnection);
        return;
    }
    if(kind == BodyItem) {
        SelectionSet sel;
        SelectionItem it;
        it.kind = SelectionItem::Kind::Body;
        it.body = item->data(0, IdRole).toString().toStdString();
        sel.add(it);
        m_view->setSelection(sel);
    }
}

void BrowserTree::onDoubleClicked(QTreeWidgetItem *item, int column) {
    if(column != 0) return;
    const Kind kind = Kind(item->data(0, KindRole).toInt());
    if(kind == SketchItem) emit editSketchRequested(item->data(0, IdRole).toInt());
    else if(kind == BodyItem) editItem(item, 0); // rename, as in Fusion
}

void BrowserTree::onChanged(QTreeWidgetItem *item, int column) {
    if(m_rebuilding || column != 0 || Kind(item->data(0, KindRole).toInt()) != BodyItem) return;
    const cad::BodyId id = item->data(0, IdRole).toString().toStdString();
    const std::string name = item->text(0).trimmed().toStdString();
    const cad::StatePtr st = m_view->state();
    const cad::Body *b = st ? st->body(id) : nullptr;
    if(b && !name.empty() && name != m_doc.bodyName(*b)) m_doc.renameBody(id, name);
}

} // namespace cadly
