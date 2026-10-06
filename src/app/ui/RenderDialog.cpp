#include "ui/RenderDialog.h"

#include "model/ModelView.h"
#include "ui/BrowserTree.h"
#include "ui/Icons.h"
#include "viewport/TraceScene.h"
#include "viewport/Viewport.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <thread>

namespace cadjitsu {

namespace {

// A row of buttons that work as one choice (a segmented control).
QButtonGroup *segmented(QWidget *parent, QBoxLayout *row, const std::vector<std::pair<QString, int>> &options) {
    auto *group = new QButtonGroup(parent);
    group->setExclusive(true);
    for(size_t i = 0; i < options.size(); ++i) {
        auto *b = new QToolButton(parent);
        b->setText(options[i].first);
        b->setCheckable(true);
        b->setAutoRaise(false);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumHeight(26);
        b->setObjectName(QStringLiteral("segment"));
        group->addButton(b, options[i].second);
        row->addWidget(b);
    }
    row->setSpacing(0);
    return group;
}

QIcon swatch(uint32_t rgb, cad::Finish finish) {
    QPixmap pm(QSize(44, 44));
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = QColor::fromRgb(QRgb(rgb));
    const QRectF r(1.5, 1.5, 19, 19);
    if(finish == cad::Finish::Translucent) {
        // A checkerboard showing through.
        p.setPen(Qt::NoPen);
        for(int y = 0; y < 4; ++y)
            for(int x = 0; x < 4; ++x) {
                p.setBrush((x + y) % 2 ? QColor(255, 255, 255) : QColor(200, 204, 210));
                p.drawRect(QRectF(1.5 + x * 4.75, 1.5 + y * 4.75, 4.75, 4.75));
            }
        p.setBrush(QColor(c.red(), c.green(), c.blue(), 165));
    } else if(finish == cad::Finish::Silk) {
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, c.lighter(165));
        g.setColorAt(0.45, c);
        g.setColorAt(1, c.darker(150));
        p.setBrush(g);
    } else {
        p.setBrush(c);
    }
    p.setPen(QPen(QColor(90, 100, 115), 1.0));
    p.drawRoundedRect(r, 4, 4);
    return QIcon(pm);
}

QGroupBox *section(const QString &title, QLayout *content) {
    auto *box = new QGroupBox(title);
    box->setLayout(content);
    return box;
}

} // namespace

RenderDialog::RenderDialog(cad::Document &doc, ModelView *view, Viewport *viewport, QWidget *parent)
    : QDialog(parent), m_doc(doc), m_view(view), m_viewport(viewport) {
    setWindowTitle(tr("Render"));
    setWindowIcon(icon(IconId::Render));
    setModal(false);
    setStyleSheet(QStringLiteral(
        "QToolButton#segment { border: 1px solid #aab6c6; background: #f4f7fb; padding: 3px 10px; }"
        "QToolButton#segment:hover { background: #e4eefb; }"
        "QToolButton#segment:checked { background: #2f7be0; color: white; border-color: #1f5fb8; }"
        "QToolButton#swatch { border: 2px solid transparent; border-radius: 6px; padding: 1px; }"
        "QToolButton#swatch:hover { border-color: #9cc0ee; }"
        "QToolButton#swatch:checked { border-color: #2f7be0; }"));

    auto *outer = new QVBoxLayout(this);

    // Bodies.
    m_bodies = new QListWidget(this);
    m_bodies->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_bodies->setMaximumHeight(110);
    m_bodies->setToolTip(tr("The bodies the material, finish and colour apply to"));
    auto *bodiesLayout = new QVBoxLayout;
    bodiesLayout->addWidget(m_bodies);
    outer->addWidget(section(tr("Bodies"), bodiesLayout));
    connect(m_bodies, &QListWidget::itemSelectionChanged, this, [this] {
        if(!m_syncing) sync();
    });

    // Material, finish, colour.
    auto *matLayout = new QFormLayout;
    auto *materialRow = new QHBoxLayout;
    m_material = segmented(this, materialRow, {{tr("PLA"), int(cad::PrintMaterial::PLA)},
                                               {tr("PETG"), int(cad::PrintMaterial::PETG)},
                                               {tr("TPU"), int(cad::PrintMaterial::TPU)}});
    matLayout->addRow(tr("Filament"), materialRow);
    auto *finishRow = new QHBoxLayout;
    m_finish = segmented(this, finishRow, {{tr("Matte"), int(cad::Finish::Matte)},
                                           {tr("Silk"), int(cad::Finish::Silk)},
                                           {tr("Semitransparent"), int(cad::Finish::Translucent)}});
    matLayout->addRow(tr("Finish"), finishRow);
    auto *colors = new QWidget(this);
    m_colorGrid = new QGridLayout(colors);
    m_colorGrid->setContentsMargins(0, 0, 0, 0);
    m_colorGrid->setSpacing(2);
    matLayout->addRow(tr("Colour"), colors);
    outer->addWidget(section(tr("Material"), matLayout));
    m_material->button(int(cad::PrintMaterial::PLA))->setToolTip(tr("PLA: stiff, easy to print, the everyday filament"));
    m_material->button(int(cad::PrintMaterial::PETG))->setToolTip(tr("PETG: tougher and more heat resistant, glossier, the clearest when semitransparent"));
    m_material->button(int(cad::PrintMaterial::TPU))->setToolTip(tr("TPU: flexible, rubbery, satin"));
    connect(m_material, &QButtonGroup::idClicked, this, [this](int id) {
        applyMaterial(cad::PrintMaterial(id), std::nullopt, std::nullopt);
    });
    connect(m_finish, &QButtonGroup::idClicked, this, [this](int id) {
        applyMaterial(std::nullopt, cad::Finish(id), std::nullopt);
    });

    // Surface.
    auto *surfLayout = new QFormLayout;
    m_layerHeight = new QDoubleSpinBox(this);
    m_layerHeight->setRange(0.04, 0.6);
    m_layerHeight->setSingleStep(0.04);
    m_layerHeight->setDecimals(2);
    m_layerHeight->setSuffix(tr(" mm"));
    m_layerLines = new QCheckBox(tr("Layer lines"), this);
    auto *surfRow = new QHBoxLayout;
    surfRow->addWidget(m_layerHeight);
    surfRow->addWidget(m_layerLines);
    surfLayout->addRow(tr("Layer height"), surfRow);
    outer->addWidget(section(tr("Surface"), surfLayout));
    connect(m_layerHeight, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        applySettings([v](cad::RenderSettings &s) { s.layerHeight = v; });
    });
    connect(m_layerLines, &QCheckBox::toggled, this, [this](bool on) {
        applySettings([on](cad::RenderSettings &s) { s.layerLines = on; });
    });

    // Build plate.
    auto *plateLayout = new QFormLayout;
    auto *plateRow = new QHBoxLayout;
    m_plate = segmented(this, plateRow, {{tr("Textured PEI"), int(cad::BuildPlateKind::TexturedPEI)},
                                         {tr("Smooth PEI"), int(cad::BuildPlateKind::SmoothPEI)},
                                         {tr("None"), int(cad::BuildPlateKind::None)}});
    plateLayout->addRow(tr("Sheet"), plateRow);
    m_placement = new QComboBox(this);
    m_placement->addItem(tr("Centred on the plate"), int(cad::Placement::Centered));
    m_placement->addItem(tr("As modelled (origin at the front left)"), int(cad::Placement::AsModelled));
    plateLayout->addRow(tr("Placement"), m_placement);
    outer->addWidget(section(tr("Build plate"), plateLayout));
    connect(m_plate, &QButtonGroup::idClicked, this, [this](int id) {
        applySettings([id](cad::RenderSettings &s) { s.plate = cad::BuildPlateKind(id); });
    });
    connect(m_placement, &QComboBox::activated, this, [this](int index) {
        const auto p = cad::Placement(m_placement->itemData(index).toInt());
        applySettings([p](cad::RenderSettings &s) { s.placement = p; });
    });

    // Scene.
    auto *sceneLayout = new QFormLayout;
    auto *lightRow = new QHBoxLayout;
    m_lighting = segmented(this, lightRow, {{tr("Studio"), int(cad::Lighting::Studio)},
                                            {tr("Daylight"), int(cad::Lighting::Daylight)}});
    sceneLayout->addRow(tr("Lighting"), lightRow);
    auto *modeRow = new QHBoxLayout;
    m_mode = segmented(this, modeRow, {{tr("Live preview"), 0}, {tr("Ray traced"), 1}});
    sceneLayout->addRow(tr("Mode"), modeRow);
    auto *qualityRow = new QHBoxLayout;
    m_quality = segmented(this, qualityRow, {{tr("Draft"), int(cad::RenderQuality::Draft)},
                                             {tr("Final"), int(cad::RenderQuality::Final)}});
    sceneLayout->addRow(tr("Quality"), qualityRow);
    outer->addWidget(section(tr("Scene"), sceneLayout));
    m_mode->button(1)->setToolTip(tr("When the view rests, the path tracer takes over and refines the picture"));
    m_quality->button(int(cad::RenderQuality::Final))->setToolTip(tr("Every screen pixel, and many more samples"));
    connect(m_lighting, &QButtonGroup::idClicked, this, [this](int id) {
        applySettings([id](cad::RenderSettings &s) { s.lighting = cad::Lighting(id); });
    });
    connect(m_mode, &QButtonGroup::idClicked, this, [this](int id) {
        applySettings([id](cad::RenderSettings &s) { s.rayTraced = id == 1; });
    });
    connect(m_quality, &QButtonGroup::idClicked, this, [this](int id) {
        applySettings([id](cad::RenderSettings &s) { s.quality = cad::RenderQuality(id); });
    });

    // Status, Save Image, Close.
    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color: #4a5a70;"));
    outer->addWidget(m_status);
    auto *bottom = new QHBoxLayout;
    m_imageSize = new QComboBox(this);
    m_imageSize->addItem(tr("Canvas size"), QSize());
    m_imageSize->addItem(QStringLiteral("1920 × 1080"), QSize(1920, 1080));
    m_imageSize->addItem(QStringLiteral("2560 × 1440"), QSize(2560, 1440));
    m_imageSize->addItem(QStringLiteral("3840 × 2160"), QSize(3840, 2160));
    m_save = new QPushButton(tr("Save Image..."), this);
    auto *close = new QPushButton(tr("Close"), this);
    bottom->addWidget(m_imageSize);
    bottom->addWidget(m_save);
    bottom->addStretch();
    bottom->addWidget(close);
    outer->addLayout(bottom);
    connect(m_save, &QPushButton::clicked, this, &RenderDialog::saveImageInteractive);
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(400);
    connect(m_statusTimer, &QTimer::timeout, this, &RenderDialog::updateStatus);
    sync();
}

void RenderDialog::showEvent(QShowEvent *e) {
    QDialog::showEvent(e);
    // What is set here shows in the Rendered style.
    if(m_viewport) m_viewport->setDisplayStyle(DisplayStyle::Rendered);
    m_statusTimer->start();
    updateStatus();
}

QAbstractButton *RenderDialog::materialButton(cad::PrintMaterial m) const { return m_material->button(int(m)); }
QAbstractButton *RenderDialog::finishButton(cad::Finish f) const { return m_finish->button(int(f)); }
QAbstractButton *RenderDialog::plateButton(cad::BuildPlateKind k) const { return m_plate->button(int(k)); }
QAbstractButton *RenderDialog::lightingButton(cad::Lighting l) const { return m_lighting->button(int(l)); }
QAbstractButton *RenderDialog::modeButton(bool rayTraced) const { return m_mode->button(rayTraced ? 1 : 0); }

void RenderDialog::selectBodies(const std::vector<cad::BodyId> &ids) {
    m_syncing = true;
    for(int i = 0; i < m_bodies->count(); ++i) {
        QListWidgetItem *it = m_bodies->item(i);
        const cad::BodyId id = it->data(Qt::UserRole).toString().toStdString();
        it->setSelected(std::find(ids.begin(), ids.end(), id) != ids.end());
    }
    m_syncing = false;
    sync();
}

std::vector<cad::BodyId> RenderDialog::targets() const {
    std::vector<cad::BodyId> ids;
    for(QListWidgetItem *it : m_bodies->selectedItems()) ids.push_back(it->data(Qt::UserRole).toString().toStdString());
    if(ids.empty())
        for(int i = 0; i < m_bodies->count(); ++i) ids.push_back(m_bodies->item(i)->data(Qt::UserRole).toString().toStdString());
    return ids;
}

void RenderDialog::sync() {
    if(m_syncing) return;
    m_syncing = true;
    // The bodies, keeping the selection.
    std::vector<cad::BodyId> selected;
    for(QListWidgetItem *it : m_bodies->selectedItems()) selected.push_back(it->data(Qt::UserRole).toString().toStdString());
    const bool firstFill = m_bodies->count() == 0;
    m_bodies->clear();
    const cad::StatePtr st = m_view ? m_view->state() : nullptr;
    if(st) {
        for(const cad::Body *b : st->orderedBodies()) {
            const cad::BodyMaterial m = m_doc.bodyMaterial(b->id);
            auto *it = new QListWidgetItem(bodyIcon(m, true), QStringLiteral("%1 — %2").arg(
                QString::fromStdString(m_doc.bodyName(*b)), QString::fromStdString(m.describe())));
            it->setData(Qt::UserRole, QString::fromStdString(b->id));
            m_bodies->addItem(it);
            it->setSelected(std::find(selected.begin(), selected.end(), b->id) != selected.end());
        }
    }
    if(firstFill && m_bodies->selectedItems().isEmpty())
        for(int i = 0; i < m_bodies->count(); ++i) m_bodies->item(i)->setSelected(true);

    // The first target's material.
    const std::vector<cad::BodyId> ids = targets();
    const cad::BodyMaterial m = ids.empty() ? cad::defaultBodyMaterial() : m_doc.bodyMaterial(ids.front());
    m_material->button(int(m.material))->setChecked(true);
    m_finish->button(int(m.finish))->setChecked(true);
    rebuildColors(m.finish, m.rgb);
    for(QAbstractButton *b : m_material->buttons()) b->setEnabled(!ids.empty());
    for(QAbstractButton *b : m_finish->buttons()) b->setEnabled(!ids.empty());

    const cad::RenderSettings &s = m_doc.renderSettings();
    m_layerHeight->setValue(s.layerHeight);
    m_layerLines->setChecked(s.layerLines);
    m_plate->button(int(s.plate))->setChecked(true);
    m_placement->setCurrentIndex(m_placement->findData(int(s.placement)));
    m_placement->setEnabled(s.plate != cad::BuildPlateKind::None);
    m_lighting->button(int(s.lighting))->setChecked(true);
    m_mode->button(s.rayTraced ? 1 : 0)->setChecked(true);
    m_quality->button(int(s.quality))->setChecked(true);
    m_syncing = false;
    updateStatus();
}

void RenderDialog::rebuildColors(cad::Finish finish, uint32_t rgb) {
    for(QToolButton *b : m_colorButtons) {
        m_colorGrid->removeWidget(b);
        b->hide();
        b->deleteLater();
    }
    m_colorButtons.clear();
    if(m_custom) {
        m_colorGrid->removeWidget(m_custom);
        m_custom->hide();
        m_custom->deleteLater();
    }
    const int columns = 8;
    int n = 0;
    bool matched = false;
    for(const cad::FilamentColor *c : cad::colorsFor(finish)) {
        auto *b = new QToolButton(this);
        b->setObjectName(QStringLiteral("swatch"));
        b->setIcon(swatch(c->rgb, finish));
        b->setIconSize(QSize(22, 22));
        b->setCheckable(true);
        b->setChecked(c->rgb == rgb && !matched);
        matched |= b->isChecked();
        b->setToolTip(QString::fromUtf8(c->name));
        b->setProperty("rgb", uint(c->rgb));
        const uint32_t value = c->rgb;
        const std::string name = c->name;
        connect(b, &QToolButton::clicked, this, [this, value, name] { applyMaterial(std::nullopt, std::nullopt, {{value, name}}); });
        m_colorGrid->addWidget(b, n / columns, n % columns);
        m_colorButtons.push_back(b);
        ++n;
    }
    m_custom = new QToolButton(this);
    m_custom->setObjectName(QStringLiteral("swatch"));
    m_custom->setText(tr("Custom..."));
    m_custom->setCheckable(true);
    m_custom->setChecked(!matched);
    if(!matched) {
        m_custom->setIcon(swatch(rgb, finish));
        m_custom->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    m_custom->setToolTip(tr("Any colour"));
    connect(m_custom, &QToolButton::clicked, this, [this, rgb] {
        const QColor c = QColorDialog::getColor(QColor::fromRgb(QRgb(rgb)), this, tr("Filament colour"));
        if(c.isValid()) applyMaterial(std::nullopt, std::nullopt, {{uint32_t(c.rgb() & 0xffffff), std::string()}});
        else sync();
    });
    m_colorGrid->addWidget(m_custom, n / columns + 1, 0, 1, columns);
}

void RenderDialog::applyMaterial(std::optional<cad::PrintMaterial> material, std::optional<cad::Finish> finish,
                                 std::optional<std::pair<uint32_t, std::string>> color) {
    if(m_syncing) return;
    const std::vector<cad::BodyId> ids = targets();
    if(ids.empty()) return;
    std::vector<cad::BodyId> all;
    for(int i = 0; i < m_bodies->count(); ++i) all.push_back(m_bodies->item(i)->data(Qt::UserRole).toString().toStdString());
    std::vector<std::pair<cad::BodyId, std::optional<cad::BodyMaterial>>> changes;
    for(const cad::BodyId &id : ids) changes.emplace_back(id, cad::withChanges(m_doc.bodyMaterial(id), material, finish, color));
    m_doc.setBodyMaterials(changes, all);
    sync();
}

void RenderDialog::applySettings(const std::function<void(cad::RenderSettings &)> &change) {
    if(m_syncing) return;
    cad::RenderSettings s = m_doc.renderSettings();
    change(s);
    m_doc.setRenderSettings(s);
    if(m_viewport) m_viewport->setDisplayStyle(DisplayStyle::Rendered);
    sync();
}

void RenderDialog::updateStatus() {
    if(!m_viewport || m_saving) return;
    const cad::RenderSettings &s = m_doc.renderSettings();
    QString text;
    if(m_viewport->displayStyle() != DisplayStyle::Rendered) text = tr("The canvas shows another visual style.");
    else if(!s.rayTraced) text = tr("Live preview.");
    else if(m_viewport->tracedSamples() > 0)
        text = tr("Ray traced: %n sample(s)", nullptr, m_viewport->tracedSamples()) +
               (m_viewport->tracedDenoised() ? tr(", denoised") : QString()) +
               (m_viewport->tracing() ? tr(", refining...") : QString());
    else text = tr("Ray tracing starts when the view rests.");
    m_status->setText(text);
}

bool RenderDialog::saveImage(const QString &path, const QSize &size, QString *error) {
    if(!m_viewport) return false;
    const QSize s = size.isValid() ? size : m_viewport->size();
    const int samples = m_doc.renderSettings().quality == cad::RenderQuality::Final ? 256 : 64;
    const TracedPicture pic = renderPicture(m_viewport->content(), m_viewport->camera(), s, samples,
                                            m_viewport->backgroundTop(), m_viewport->backgroundBottom());
    if(!pic.image.save(path)) {
        if(error) *error = tr("Could not write %1").arg(path);
        return false;
    }
    return true;
}

void RenderDialog::saveImageInteractive() {
    if(!m_viewport || m_saving) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Image"), QStringLiteral("render.png"),
                                                      tr("PNG images (*.png);;JPEG images (*.jpg)"));
    if(path.isEmpty()) return;
    QSize size = m_imageSize->currentData().toSize();
    if(!size.isValid()) size = m_viewport->size() * m_viewport->devicePixelRatioF();
    const int samples = m_doc.renderSettings().quality == cad::RenderQuality::Final ? 256 : 64;
    // In the background: the scene and camera as they are now.
    m_saving = true;
    m_save->setEnabled(false);
    m_status->setText(tr("Rendering %1 × %2, %3 samples...").arg(size.width()).arg(size.height()).arg(samples));
    QPointer<RenderDialog> self(this);
    std::thread([self, scene = m_viewport->content(), camera = m_viewport->camera(), size, samples, path,
                 top = m_viewport->backgroundTop(), bottom = m_viewport->backgroundBottom()] {
        const TracedPicture pic = renderPicture(scene, camera, size, samples, top, bottom);
        const bool ok = pic.image.save(path);
        QMetaObject::invokeMethod(qApp, [self, ok, path] {
            if(!self) return;
            self->m_saving = false;
            self->m_save->setEnabled(true);
            self->m_status->setText(ok ? tr("Saved %1").arg(path) : tr("Could not write %1").arg(path));
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace cadjitsu
