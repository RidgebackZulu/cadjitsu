#include "sketch/SketchPalette.h"

#include "ui/Icons.h"
#include "viewport/ViewCube.h"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace cadly {

SketchPalette::SketchPalette(QWidget *canvas) : QFrame(canvas) {
    setObjectName(QStringLiteral("sketchPalette"));
    setStyleSheet(QStringLiteral(
        "#sketchPalette { background: rgba(250, 251, 253, 238); border: 1px solid rgba(120, 130, 145, 120);"
        " border-radius: 6px; }"
        "#sketchPaletteTitle { font-weight: 700; font-size: 10px; color: #3c4450; letter-spacing: 1px; }"
        "QCheckBox { font-size: 11px; color: #2c333d; }"
        "QToolButton { border-radius: 4px; padding: 3px; font-size: 11px; }"
        "QToolButton:hover { background: rgba(60, 130, 220, 40); }"
        "#finishSketch { background: #4c9a4c; color: white; font-weight: 600; border-radius: 4px; padding: 5px 10px;"
        " border: 1px solid #3a7c3a; }"
        "#finishSketch:hover { background: #57aa57; }"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(10, 8, 10, 10);
    v->setSpacing(4);
    auto *title = new QLabel(tr("SKETCH PALETTE"), this);
    title->setObjectName(QStringLiteral("sketchPaletteTitle"));
    v->addWidget(title);

    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    m_lookAt = new QToolButton(this);
    m_lookAt->setObjectName(QStringLiteral("paletteLookAt"));
    m_lookAt->setIcon(icon(IconId::LookAt));
    m_lookAt->setText(tr("Look At"));
    m_lookAt->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_lookAt->setFocusPolicy(Qt::NoFocus);
    m_construction = new QToolButton(this);
    m_construction->setObjectName(QStringLiteral("paletteConstruction"));
    m_construction->setIcon(icon(IconId::Construction));
    m_construction->setText(tr("Construction"));
    m_construction->setToolTip(tr("Normal / Construction (X)"));
    m_construction->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_construction->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_lookAt);
    row->addWidget(m_construction);
    v->addLayout(row);

    auto box = [&](const QString &text, const char *name) {
        auto *c = new QCheckBox(text, this);
        c->setObjectName(QString::fromLatin1(name));
        c->setFocusPolicy(Qt::NoFocus);
        v->addWidget(c);
        connect(c, &QCheckBox::toggled, this, [this] { emit optionsChanged(options()); });
        return c;
    };
    m_grid = box(tr("Sketch Grid"), "paletteGrid");
    m_snap = box(tr("Snap to Grid"), "paletteSnap");
    m_profile = box(tr("Show Profile"), "paletteProfile");
    m_points = box(tr("Show Points"), "palettePoints");
    m_dimensions = box(tr("Show Dimensions"), "paletteDimensions");
    m_constraints = box(tr("Show Constraints"), "paletteConstraints");

    v->addSpacing(6);
    m_finish = new QPushButton(icon(IconId::FinishSketch), tr("Finish Sketch"), this);
    m_finish->setObjectName(QStringLiteral("finishSketch"));
    m_finish->setFocusPolicy(Qt::NoFocus);
    v->addWidget(m_finish);

    connect(m_lookAt, &QToolButton::clicked, this, &SketchPalette::lookAtRequested);
    connect(m_construction, &QToolButton::clicked, this, &SketchPalette::constructionRequested);
    connect(m_finish, &QPushButton::clicked, this, &SketchPalette::finishRequested);

    setOptions(SketchDisplayOptions());
    canvas->installEventFilter(this);
    adjustSize();
    hide();
}

void SketchPalette::setOptions(const SketchDisplayOptions &o) {
    const QSignalBlocker b1(m_grid), b2(m_snap), b3(m_profile), b4(m_points), b5(m_dimensions), b6(m_constraints);
    m_grid->setChecked(o.grid);
    m_snap->setChecked(o.snapToGrid);
    m_profile->setChecked(o.profiles);
    m_points->setChecked(o.points);
    m_dimensions->setChecked(o.dimensions);
    m_constraints->setChecked(o.constraints);
}

SketchDisplayOptions SketchPalette::options() const {
    SketchDisplayOptions o;
    o.grid = m_grid->isChecked();
    o.snapToGrid = m_snap->isChecked();
    o.profiles = m_profile->isChecked();
    o.points = m_points->isChecked();
    o.dimensions = m_dimensions->isChecked();
    o.constraints = m_constraints->isChecked();
    return o;
}

void SketchPalette::reposition() {
    QWidget *canvas = parentWidget();
    if(!canvas) return;
    adjustSize();
    const QRect cube = ViewCube::rect(canvas->size());
    move(canvas->width() - width() - 12, cube.bottom() + 16);
    raise();
}

bool SketchPalette::eventFilter(QObject *o, QEvent *e) {
    if(o == parentWidget() && e->type() == QEvent::Resize) reposition();
    return QFrame::eventFilter(o, e);
}

} // namespace cadly
