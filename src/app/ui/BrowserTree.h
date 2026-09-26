#pragma once

#include "model/RecomputeService.h"

#include "doc/Document.h"

#include <QTreeWidget>

namespace cadly {

class ModelView;

// Fusion 360's BROWSER: the design's Origin, Analysis (section analyses),
// Bodies, Sketches and Construction folders, with eye icons to show / hide
// each item, in-place renaming of bodies, and double-click to edit a sketch
// or a section analysis.
class BrowserTree : public QTreeWidget {
    Q_OBJECT

public:
    BrowserTree(cad::Document &doc, ModelView *view, QWidget *parent = nullptr);

    void setDocumentName(const QString &name);
    // Rebuilds the folders from the displayed evaluation.
    void rebuild();

    QTreeWidgetItem *folder(const QString &name) const;

signals:
    void editSketchRequested(cad::FeatureId id);
    void editSectionRequested(int id);

protected:
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    enum Role { KindRole = Qt::UserRole + 1, IdRole };
    enum Kind {
        Root,
        OriginFolder,
        BodiesFolder,
        SketchesFolder,
        ConstructionFolder,
        BodyItem,
        SketchItem,
        PlaneItem,
        AnalysisFolder,
        SectionItem
    };

    void onClicked(QTreeWidgetItem *item, int column);
    void onDoubleClicked(QTreeWidgetItem *item, int column);
    void onChanged(QTreeWidgetItem *item, int column);
    void setEye(QTreeWidgetItem *item, bool visible);

    cad::Document &m_doc;
    ModelView *m_view;
    QString m_name = QStringLiteral("Untitled");
    bool m_rebuilding = false;
};

} // namespace cadly
