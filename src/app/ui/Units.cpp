#include "ui/Units.h"

#include <QEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSettings>

#include <cmath>

namespace cadjitsu {

namespace units {

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180.0;

const QStringList kLength = {QStringLiteral("mm"), QStringLiteral("cm"), QStringLiteral("m"), QStringLiteral("in"),
                             QStringLiteral("ft")};
const QStringList kAngle = {QStringLiteral("deg"), QStringLiteral("rad")};

// Spellings the expression parser also takes, to the drop-down's names.
QString canonical(const QString &u) {
    if(u == QLatin1String("inch") || u == QLatin1String("\"")) return QStringLiteral("in");
    if(u == QString(QChar(0x00B0))) return QStringLiteral("deg");
    return u;
}

} // namespace

QStringList choices(cad::ValueKind kind) {
    if(kind == cad::ValueKind::Length) return kLength;
    if(kind == cad::ValueKind::Angle) return kAngle;
    return {};
}

QString defaultUnit(cad::ValueKind kind) {
    if(kind == cad::ValueKind::Angle) return QStringLiteral("deg");
    if(kind != cad::ValueKind::Length) return {};
    const QString u = QSettings().value(QStringLiteral("units/length"), QStringLiteral("mm")).toString();
    return kLength.contains(u) ? u : QStringLiteral("mm");
}

void setDefaultLengthUnit(const QString &unit) {
    if(kLength.contains(unit)) QSettings().setValue(QStringLiteral("units/length"), unit);
}

double factor(const QString &unit) {
    const QString u = canonical(unit);
    if(u == QLatin1String("cm")) return 10.0;
    if(u == QLatin1String("m")) return 1000.0;
    if(u == QLatin1String("um")) return 0.001;
    if(u == QLatin1String("in")) return 25.4;
    if(u == QLatin1String("ft")) return 304.8;
    if(u == QLatin1String("deg")) return kDeg;
    return 1.0; // mm, rad
}

bool isNumber(const QString &text, double *value) {
    static const QRegularExpression re(QStringLiteral(R"(^\s*[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?\s*$)"));
    if(!re.match(text).hasMatch()) return false;
    if(value) *value = text.trimmed().toDouble();
    return true;
}

bool splitQuantity(const QString &text, double &number, QString &unit) {
    static const QRegularExpression re(QStringLiteral(
        R"(^\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*(mm|cm|m|um|in|inch|ft|"|deg|°|rad)?\s*$)"));
    const auto m = re.match(text);
    if(!m.hasMatch()) return false;
    number = m.captured(1).toDouble();
    unit = canonical(m.captured(2));
    return true;
}

QString formatNumber(double v) {
    if(std::fabs(v) < 5e-7) v = 0.0;
    QString s = QString::number(v, 'f', 6);
    if(s.contains(QLatin1Char('.'))) {
        while(s.endsWith(QLatin1Char('0'))) s.chop(1);
        if(s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

QString displayText(const QString &expr, cad::ValueKind kind) {
    double n;
    QString u;
    const QString def = defaultUnit(kind);
    if(def.isEmpty() || !splitQuantity(expr, n, u)) return expr.trimmed();
    if(u.isEmpty()) u = kind == cad::ValueKind::Angle ? QStringLiteral("deg") : QStringLiteral("mm");
    if(u == def) return formatNumber(n);
    return formatNumber(n * factor(u) / factor(def));
}

QString toExpression(const QString &text, const QString &unit, cad::ValueKind kind) {
    double n;
    const QString def = defaultUnit(kind);
    if(def.isEmpty() || !isNumber(text, &n)) return text.trimmed();
    const QString u = unit.isEmpty() ? def : unit;
    return formatNumber(u == def ? n : n * factor(u) / factor(def)) + QLatin1Char(' ') + def;
}

QString formatInDefault(double base, cad::ValueKind kind, int decimals) {
    const QString def = defaultUnit(kind);
    if(def.isEmpty()) return QString::number(base, 'g', 6);
    const double v = base / factor(def);
    if(decimals < 0) return formatNumber(v);
    return QString::number(v, 'f', decimals);
}

} // namespace units

UnitSuffix *UnitSuffix::attach(QLineEdit *edit, cad::ValueKind kind) {
    if(units::choices(kind).isEmpty()) return nullptr;
    return new UnitSuffix(edit, kind);
}

UnitSuffix::UnitSuffix(QLineEdit *edit, cad::ValueKind kind) : QToolButton(edit), m_edit(edit), m_kind(kind) {
    setObjectName(QStringLiteral("unitSuffix"));
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Unit of the number typed here (converted to the default unit, Settings > Units)"));
    m_unit = units::defaultUnit(kind);
    auto *menu = new QMenu(this);
    for(const QString &u : units::choices(kind)) {
        QAction *a = menu->addAction(u);
        connect(a, &QAction::triggered, this, [this, u] {
            if(u == m_unit) return;
            setUnit(u);
            emit unitPicked(u);
        });
    }
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        for(QAction *a : menu->actions()) {
            a->setCheckable(true);
            a->setChecked(a->text() == m_unit);
        }
    });
    setMenu(menu);
    setPopupMode(QToolButton::InstantPopup);
    edit->installEventFilter(this);
    place();
    show();
}

void UnitSuffix::setUnit(const QString &unit) {
    if(unit == m_unit) return;
    m_unit = unit;
    place();
    update();
}

void UnitSuffix::setActive(bool on) {
    if(on == m_active) return;
    m_active = on;
    update();
}

QSize UnitSuffix::sizeHint() const {
    QFont f = font();
    f.setPixelSize(11);
    const int w = QFontMetrics(f).horizontalAdvance(m_unit) + 17;
    return {std::max(w, 30), 18};
}

void UnitSuffix::place() {
    const QSize s = sizeHint();
    const int h = std::min(s.height(), m_edit->height() - 4);
    resize(s.width(), h);
    move(m_edit->width() - s.width() - 2, (m_edit->height() - h) / 2);
    // Keep the typed text clear of it.
    const QMargins m = m_edit->textMargins();
    if(m.right() != s.width() + 2) m_edit->setTextMargins(m.left(), m.top(), s.width() + 2, m.bottom());
}

bool UnitSuffix::eventFilter(QObject *o, QEvent *e) {
    if(o == m_edit && (e->type() == QEvent::Resize || e->type() == QEvent::Show)) place();
    return QToolButton::eventFilter(o, e);
}

void UnitSuffix::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const bool hot = underMouse() || isDown();
    p.setPen(Qt::NoPen);
    p.setBrush(hot ? QColor(215, 230, 250) : QColor(236, 241, 248));
    p.drawRoundedRect(r, 3, 3);
    const QColor ink = m_active ? QColor(52, 66, 86) : QColor(160, 170, 184);
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(ink);
    const QRectF text(r.left() + 4, r.top(), r.width() - 15, r.height());
    p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, m_unit);
    // A small open chevron.
    const double cx = r.right() - 6.5, cy = r.center().y();
    QPainterPath v;
    v.moveTo(cx - 3, cy - 1.5);
    v.lineTo(cx, cy + 1.5);
    v.lineTo(cx + 3, cy - 1.5);
    p.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(v);
}

} // namespace cadjitsu
