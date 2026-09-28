#include "command/OverhangCommand.h"

#include "model/ModelView.h"
#include "ui/Icons.h"

#include <QLabel>
#include <QSettings>

namespace cadjitsu {

namespace {
const char *kLimitKey = "overhang/limit";

QString area(double mm2) { return QStringLiteral("%1 mm²").arg(mm2, 0, 'f', 1); }
} // namespace

IconId OverhangCommand::iconId() const { return IconId::Overhang; }

void OverhangCommand::setup() {
    CommandPanel &panel = *m_ctx.panel;
    m_limit = panel.addAngle(tr("Overhang limit"), evaluator(), "overhangLimit", QColor(225, 50, 45));
    m_support = panel.addInfo(tr("Needs support"), "overhangSupport");
    m_bridges = panel.addInfo(tr("Bridges"), "overhangBridges");
    m_near = panel.addInfo(tr("Near the limit"), "overhangNear");
    m_legend = panel.addInfo(QString(), "overhangLegend");
    m_legend->setTextFormat(Qt::RichText);
    m_legend->setStyleSheet(QStringLiteral("color: #3c4450; font-weight: normal;"));
    m_legend->setText(tr("<span style='color:#e1322d'>■</span> needs support<br>"
                         "<span style='color:#f5aa1e'>■</span> near the limit<br>"
                         "<span style='color:#286ee6'>■</span> flat bridge<br>"
                         "<span style='color:#3cb45a'>■</span> prints fine"));
    const double deg = QSettings().value(QString::fromLatin1(kLimitKey), 45.0).toDouble();
    m_limit->setExpression(QStringLiteral("%1 deg").arg(deg));
    connect(m_limit, &ValueField::edited, this, &Command::inputsChanged);
    m_limit->setFocus(Qt::OtherFocusReason);
    m_limit->selectAll();
}

void OverhangCommand::showPreview() {
    if(!m_limit->valid()) {
        m_ctx.panel->setMessage(tr("Overhang limit: enter an angle"), cad::Severity::Warning);
        return;
    }
    const double deg = std::clamp(*m_limit->value() * 180.0 / M_PI, 0.0, 89.0);
    QSettings().setValue(QString::fromLatin1(kLimitKey), deg);
    cad::OverhangOptions o;
    o.threshold = deg;
    m_ctx.view->setOverhangAnalysis(o);
    const auto &a = m_ctx.view->overhangAreas();
    m_support->setText(area(a[size_t(cad::OverhangKind::Overhang)]));
    m_bridges->setText(area(a[size_t(cad::OverhangKind::Bridge)]));
    m_near->setText(area(a[size_t(cad::OverhangKind::Near)]));
    const bool clean = a[size_t(cad::OverhangKind::Overhang)] < 0.05 && a[size_t(cad::OverhangKind::Bridge)] < 0.05;
    m_ctx.panel->setMessage(clean ? tr("Prints without support at this limit.")
                                  : tr("Add supports, or chamfer / draft the red faces."));
}

void OverhangCommand::end() { m_ctx.view->setOverhangAnalysis(std::nullopt); }

} // namespace cadjitsu
