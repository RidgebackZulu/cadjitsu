#include "ui/BrowserTree.h"

#include "model/CanvasPicture.h"

#include "model/ModelView.h"
#include "ui/Icons.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QStyledItemDelegate>

#include <set>

namespace cadjitsu {

// The body icon with a dot of its filament colour (none when it has the default).
QIcon bodyIcon(const cad::BodyMaterial &m, bool chosen) {
    const QIcon base = icon(IconId::Body);
    if(!chosen) return base;
    QIcon out;
    for(int size : {16, 32}) {
        QPixmap pm = base.pixmap(QSize(size, size), 2.0);
        pm.setDevicePixelRatio(2.0);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor c = QColor::fromRgb(QRgb(m.rgb));
        const double r = size * 0.24;
        const QPointF centre(size - r - 0.5, size - r - 0.5);
        p.setPen(QPen(QColor(255, 255, 255), size / 16.0));
        p.setBrush(m.finish == cad::Finish::Translucent ? QColor(c.red(), c.green(), c.blue(), 150) : c);
        p.drawEllipse(centre, r, r);
        if(m.finish == cad::Finish::Silk) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 170));
            p.drawEllipse(centre - QPointF(r * 0.35, r * 0.35), r * 0.3, r * 0.3);
        }
        p.end();
        out.addPixmap(pm);
    }
    return out;
}

namespace {

constexpr int OverriddenRole = Qt::UserRole + 20;

// The eye column: the eye sits in a small rounded button that lights up on
// hover, so it reads as something to click.
class EyeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const QIcon ic = index.data(Qt::DecorationRole).value<QIcon>();
        if(ic.isNull()) return QStyledItemDelegate::paint(p, option, index);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF cell = QRectF(option.rect).adjusted(2.5, 1.5, -2.5, -1.5);
        if(option.state & QStyle::State_MouseOver) {
            p->setPen(QPen(QColor(47, 123, 224, 120), 1));
            p->setBrush(QColor(47, 123, 224, 38));
            p->drawRoundedRect(cell, 4, 4);
        }
        // An item inside a hidden folder: its own setting, faded (the folder overrides it).
        const bool overridden = index.data(OverriddenRole).toBool();
        const QRect ir(0, 0, 16, 16);
        ic.paint(p, QRect(ir.translated(option.rect.center() - ir.center())), Qt::AlignCenter,
                 overridden ? QIcon::Disabled : QIcon::Normal);
        p->restore();
    }
};

} // namespace

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
    setItemDelegateForColumn(1, new EyeDelegate(this));
    setMouseTracking(true);
    setIndentation(14);
    setFocusPolicy(Qt::ClickFocus);
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    setStyleSheet(QStringLiteral("#browser { background: rgba(250, 251, 253, 240); border: none;"
                                 " border-right: 1px solid #c9ced6; font-size: 12px; color: #1c2128; }"
                                 "#browser::item { color: #1c2128; padding: 1px 0; }"
                                 "#browser::item:selected { background: #cfe0f7; color: #0f1a2a; }"
                                 "#browser::item:hover:!selected { background: #e6eef9; }"));
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
    QTreeWidgetItem *analysis = makeFolder(tr("Analysis"), AnalysisFolder);
    for(const auto &s : m_doc.sections()) {
        auto *it = new QTreeWidgetItem(analysis, {QString::fromStdString(s.name)});
        it->setData(0, KindRole, SectionItem);
        it->setData(0, IdRole, s.id);
        it->setIcon(0, icon(IconId::Section));
        setEye(it, s.visible);
    }

    const cad::StatePtr st = m_view->state();
    QTreeWidgetItem *bodies = makeFolder(tr("Bodies"), BodiesFolder);
    QTreeWidgetItem *canvases = makeFolder(tr("Canvases"), CanvasesFolder);
    const bool canvasesOn = m_doc.folderVisible("canvases");
    setEye(canvases, canvasesOn);
    QTreeWidgetItem *sketches = makeFolder(tr("Sketches"), SketchesFolder);
    QTreeWidgetItem *construction = makeFolder(tr("Construction"), ConstructionFolder);
    // Folder eyes: hide or show everything inside, whatever each item says.
    const bool bodiesOn = m_doc.folderVisible("bodies"), sketchesOn = m_doc.folderVisible("sketches"),
               planesOn = m_doc.folderVisible("construction");
    setEye(bodies, bodiesOn);
    setEye(sketches, sketchesOn);
    setEye(construction, planesOn);
    auto child = [](QTreeWidgetItem *it, bool folderOn) {
        it->setData(1, OverriddenRole, !folderOn);
        if(!folderOn) it->setToolTip(1, it->toolTip(1) + tr(" (the folder is hidden)"));
    };
    for(const auto &c : m_doc.canvases()) {
        auto *it = new QTreeWidgetItem(canvases, {QString::fromStdString(c.name)});
        it->setData(0, KindRole, CanvasItem);
        it->setData(0, IdRole, c.id);
        it->setIcon(0, icon(IconId::Canvas));
        it->setToolTip(0, tr("%1 mm wide; right-click to calibrate it").arg(c.width(), 0, 'f', 1));
        setEye(it, c.visible);
        child(it, canvasesOn);
    }
    if(st) {
        for(const cad::Body *b : st->orderedBodies()) {
            auto *it = new QTreeWidgetItem(bodies, {QString::fromStdString(m_doc.bodyName(*b))});
            it->setData(0, KindRole, BodyItem);
            it->setData(0, IdRole, QString::fromStdString(b->id));
            it->setIcon(0, bodyIcon(m_doc.bodyMaterial(b->id), m_doc.explicitBodyMaterial(b->id).has_value()));
            it->setToolTip(0, QString::fromStdString(m_doc.bodyMaterial(b->id).describe()));
            it->setFlags(it->flags() | Qt::ItemIsEditable);
            setEye(it, m_doc.bodyVisible(b->id));
            child(it, bodiesOn);
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
            setEye(it, m_view->sketchShown(fid, true));
            child(it, sketchesOn);
        }
        for(const auto &[fid, plane] : st->planes) {
            auto *it = new QTreeWidgetItem(construction, {QString::fromStdString(plane->name)});
            it->setData(0, KindRole, PlaneItem);
            it->setData(0, IdRole, fid);
            it->setIcon(0, icon(IconId::PlaneNode));
            setEye(it, m_doc.planeVisible(fid));
            child(it, planesOn);
        }
    }
    root->setExpanded(true);
    for(QTreeWidgetItem *f : {origin, analysis, bodies, canvases, sketches, construction}) {
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
            m_doc.setSketchVisible(id, !m_view->sketchShown(id, true));
            break;
        }
        case PlaneItem: {
            const cad::FeatureId id = item->data(0, IdRole).toInt();
            m_doc.setPlaneVisible(id, !m_doc.planeVisible(id));
            break;
        }
        case CanvasItem: {
            const int id = item->data(0, IdRole).toInt();
            if(const cad::ReferenceImage *c = m_doc.canvas(id)) m_doc.setCanvasVisible(id, !c->visible);
            break;
        }
        case BodiesFolder:
        case SketchesFolder:
        case CanvasesFolder:
        case ConstructionFolder: {
            const std::string folder = kind == BodiesFolder     ? "bodies"
                                       : kind == SketchesFolder ? "sketches"
                                       : kind == CanvasesFolder ? "canvases"
                                                                : "construction";
            m_doc.setFolderVisible(folder, !m_doc.folderVisible(folder));
            break;
        }
        case SectionItem: {
            // Off and back to the normal view, or on (instead of any other section).
            const int id = item->data(0, IdRole).toInt();
            if(const cad::SectionAnalysis *s = m_doc.section(id)) m_doc.setSectionVisible(id, !s->visible);
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
    else if(kind == SectionItem) emit editSectionRequested(item->data(0, IdRole).toInt());
    else if(kind == CanvasItem) emit editCanvasRequested(item->data(0, IdRole).toInt());
    else if(kind == BodyItem) editItem(item, 0); // rename, as in Fusion
}

void BrowserTree::contextMenuEvent(QContextMenuEvent *e) {
    QTreeWidgetItem *item = itemAt(e->pos());
    if(!item) return;
    const Kind kind = Kind(item->data(0, KindRole).toInt());
    if(kind == BodyItem) {
        // The bodies selected in the browser, or this one.
        std::vector<cad::BodyId> ids;
        if(item->isSelected())
            for(QTreeWidgetItem *it : selectedItems())
                if(Kind(it->data(0, KindRole).toInt()) == BodyItem) ids.push_back(it->data(0, IdRole).toString().toStdString());
        if(ids.empty()) ids.push_back(item->data(0, IdRole).toString().toStdString());
        QMenu menu(this);
        menu.addAction(icon(IconId::Render), tr("Material..."), this, [this, ids] { emit materialRequested(ids); });
        menu.exec(e->globalPos());
        return;
    }
    if(kind == SketchItem) {
        const cad::FeatureId id = item->data(0, IdRole).toInt();
        QMenu menu(this);
        menu.addAction(icon(IconId::Sketch), tr("Edit Sketch"), this, [this, id] { emit editSketchRequested(id); });
        menu.addSeparator();
        menu.addAction(icon(IconId::Delete), tr("Delete"), this, [this, id] { emit deleteSketchRequested(id); });
        menu.exec(e->globalPos());
        return;
    }
    if(kind == CanvasItem) {
        const int id = item->data(0, IdRole).toInt();
        const cad::ReferenceImage *c = m_doc.canvas(id);
        if(!c) return;
        QMenu menu(this);
        menu.addAction(icon(IconId::Canvas), tr("Edit Canvas"), this, [this, id] { emit editCanvasRequested(id); });
        menu.addAction(icon(IconId::Measure), tr("Calibrate..."), this, [this, id] { emit calibrateCanvasRequested(id); });
        menu.addAction(tr("Correct Perspective..."), this, [this, id] { emit perspectiveCanvasRequested(id); });
        if(c->perspective)
            menu.addAction(tr("Remove Perspective Correction"), this, [this, id] {
                if(const cad::ReferenceImage *old = m_doc.canvas(id)) {
                    cad::ReferenceImage r = *old;
                    const QImage photo = canvasPhoto(m_doc, r);
                    if(photo.isNull()) return;
                    // The photo as taken, as wide as the corrected picture was.
                    r.mmPerPixel = r.width() / photo.width();
                    r.pixelWidth = photo.width();
                    r.pixelHeight = photo.height();
                    r.perspective.reset();
                    m_doc.updateCanvas(r, true, "Remove Perspective Correction");
                }
            });
        menu.addSeparator();
        menu.addAction(c->visible ? tr("Hide") : tr("Show"), this, [this, id, on = !c->visible] { m_doc.setCanvasVisible(id, on); });
        menu.addAction(icon(IconId::Delete), tr("Delete"), this, [this, id] { m_doc.deleteCanvas(id); });
        menu.exec(e->globalPos());
        return;
    }
    if(kind != SectionItem) return;
    const int id = item->data(0, IdRole).toInt();
    const cad::SectionAnalysis *s = m_doc.section(id);
    if(!s) return;
    QMenu menu(this);
    menu.addAction(tr("Edit Section Analysis"), this, [this, id] { emit editSectionRequested(id); });
    menu.addAction(s->visible ? tr("Hide") : tr("Show"), this, [this, id, on = !s->visible] {
        m_doc.setSectionVisible(id, on);
    });
    menu.addAction(tr("Delete"), this, [this, id] { m_doc.deleteSection(id); });
    menu.exec(e->globalPos());
}

void BrowserTree::onChanged(QTreeWidgetItem *item, int column) {
    if(m_rebuilding || column != 0 || Kind(item->data(0, KindRole).toInt()) != BodyItem) return;
    const cad::BodyId id = item->data(0, IdRole).toString().toStdString();
    const std::string name = item->text(0).trimmed().toStdString();
    const cad::StatePtr st = m_view->state();
    const cad::Body *b = st ? st->body(id) : nullptr;
    if(b && !name.empty() && name != m_doc.bodyName(*b)) m_doc.renameBody(id, name);
}

} // namespace cadjitsu
