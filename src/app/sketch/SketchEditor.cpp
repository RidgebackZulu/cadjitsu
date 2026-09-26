#include "sketch/SketchEditor.h"

#include "sketch/ProfileMesh.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include "measure/Measure.h"

#include <QFontMetricsF>
#include <QIcon>
#include <QLineF>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace cadly {

using cad::SkCon;
using cad::SkConstraint;
using cad::SkEntity;
using cad::SkType;
using cad::Vec2;
using Kind = SketchHit::Kind;

namespace {

const QColor kFixedColor(24, 26, 30);
const QColor kFreeColor(26, 101, 201);
const QColor kConstructionColor(240, 140, 0);
const QColor kSelectedColor(58, 163, 255);
const QColor kHoverColor(120, 185, 255);
const QColor kFailedColor(214, 44, 44);
const QColor kProfileColor(255, 200, 120, 80);
const QColor kProfileHoverColor(255, 178, 80, 125);
const QColor kProfileSelectedColor(80, 150, 235, 115);
const QColor kDimensionColor(40, 46, 56);
const QColor kDrivenColor(125, 132, 142);

constexpr double kPointTol = 7.0; // pixels
constexpr double kCurveTol = 6.0;
constexpr double kSnapTol = 9.0;
constexpr int kCircleSegments = 96;

// Projects world points to logical pixels with a fixed camera.
struct Projector {
    QMatrix4x4 viewProj;
    double width = 1, height = 1;

    explicit Projector(const Viewport *vp) {
        Camera c = vp->camera();
        c.viewport = vp->size();
        viewProj = c.viewProjection();
        width = c.viewport.width();
        height = c.viewport.height();
    }
    QPointF operator()(const QVector3D &p) const {
        const QVector4D c = viewProj * QVector4D(p, 1.0f);
        const double iw = std::fabs(c.w()) > 1e-12f ? 1.0 / c.w() : 0.0;
        return QPointF((c.x() * iw + 1.0) * 0.5 * width, (1.0 - c.y() * iw) * 0.5 * height);
    }
};

double pxDist(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

double segDist(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a, ap = p - a;
    const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
    const double t = l2 > 1e-12 ? std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / l2, 0.0, 1.0) : 0.0;
    return pxDist(p, a + ab * t);
}

bool contains(const std::vector<int> &v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

double arcSweep(Vec2 c, Vec2 s, Vec2 e) {
    double sw = cad::normAngle((e - c).angle() - (s - c).angle());
    if(sw < 1e-9) sw = 2 * cad::kPi;
    return sw;
}

std::string trimNumber(double v, int decimals) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(decimals) << v;
    std::string s = os.str();
    if(s.find('.') != std::string::npos) {
        while(!s.empty() && s.back() == '0') s.pop_back();
        if(!s.empty() && s.back() == '.') s.pop_back();
    }
    if(s == "-0") s = "0";
    return s;
}

void drawArrow(QPainter &p, QPointF tip, QPointF dir, const QColor &color) {
    const double l = std::hypot(dir.x(), dir.y());
    if(l < 1e-6) return;
    const QPointF u = dir / l, n(-u.y(), u.x());
    const QPointF base = tip - u * 8.0;
    QPolygonF tri;
    tri << tip << base + n * 2.8 << base - n * 2.8;
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(tri);
    p.restore();
}

IconId glyphIcon(SkCon t) {
    switch(t) {
    case SkCon::Coincident:
    case SkCon::PointOnCurve: return IconId::Coincident;
    case SkCon::Horizontal: return IconId::Horizontal;
    case SkCon::Vertical: return IconId::Vertical;
    case SkCon::Parallel: return IconId::Parallel;
    case SkCon::Perpendicular: return IconId::Perpendicular;
    case SkCon::Tangent: return IconId::Tangent;
    case SkCon::Equal: return IconId::Equal;
    case SkCon::Midpoint: return IconId::Midpoint;
    case SkCon::Concentric: return IconId::Concentric;
    case SkCon::Symmetric: return IconId::Symmetric;
    case SkCon::Fix: return IconId::Fix;
    default: return IconId::Dimension;
    }
}

} // namespace

// ---------------------------------------------------------------------------

SketchEditor::SketchEditor(Viewport *viewport, const cad::SketchFeature &feature, const gp_Ax3 &frame,
                           ParamProvider params, ParamAllocator allocate, QObject *parent)
    : QObject(parent), m_viewport(viewport), m_feature(std::make_shared<cad::SketchFeature>(feature)), m_frame(frame),
      m_paramProvider(std::move(params)), m_allocate(std::move(allocate)) {
    solve();
}

void SketchEditor::setOptions(const SketchDisplayOptions &o) {
    m_options = o;
    refreshView();
}

// --- coordinates -------------------------------------------------------------------

std::optional<Vec2> SketchEditor::toSketch(QPointF px) const {
    Camera cam = m_viewport->camera();
    cam.viewport = m_viewport->size();
    QVector3D o, d;
    cam.ray(px, o, d);
    const gp_XYZ origin(o.x(), o.y(), o.z()), dir(d.x(), d.y(), d.z());
    const gp_XYZ n = m_frame.Direction().XYZ();
    const double denom = dir.Dot(n);
    if(std::fabs(denom) < 1e-9) return std::nullopt;
    const double t = (m_frame.Location().XYZ() - origin).Dot(n) / denom;
    const gp_XYZ rel = origin + dir * t - m_frame.Location().XYZ();
    return Vec2(rel.Dot(m_frame.XDirection().XYZ()), rel.Dot(m_frame.YDirection().XYZ()));
}

QVector3D SketchEditor::toWorld(Vec2 p) const {
    const gp_XYZ w = m_frame.Location().XYZ() + m_frame.XDirection().XYZ() * p.x + m_frame.YDirection().XYZ() * p.y;
    return QVector3D(float(w.X()), float(w.Y()), float(w.Z()));
}

QPointF SketchEditor::toScreen(Vec2 p) const { return Projector(m_viewport)(toWorld(p)); }

double SketchEditor::sketchUnitsPerPixel(Vec2 near) const {
    const Projector P(m_viewport);
    const QPointF a = P(toWorld(near));
    const double l = std::max(pxDist(a, P(toWorld(near + Vec2(1, 0)))), pxDist(a, P(toWorld(near + Vec2(0, 1)))));
    return l > 1e-9 ? 1.0 / l : 1.0;
}

QMatrix4x4 SketchEditor::frameMatrix() const {
    const gp_XYZ o = m_frame.Location().XYZ(), x = m_frame.XDirection().XYZ(), y = m_frame.YDirection().XYZ(),
                 n = m_frame.Direction().XYZ();
    return QMatrix4x4(float(x.X()), float(y.X()), float(n.X()), float(o.X()), //
                      float(x.Y()), float(y.Y()), float(n.Y()), float(o.Y()), //
                      float(x.Z()), float(y.Z()), float(n.Z()), float(o.Z()), //
                      0.0f, 0.0f, 0.0f, 1.0f);
}

Vec2 SketchEditor::posOf(int pointId) const { return pointId == cad::kSketchOrigin ? Vec2() : sketch().pointPos(pointId); }

bool SketchEditor::lineEnds(int lineId, Vec2 &a, Vec2 &b) const {
    if(lineId == cad::kSketchXAxis) {
        a = Vec2(0, 0);
        b = Vec2(1, 0);
        return true;
    }
    if(lineId == cad::kSketchYAxis) {
        a = Vec2(0, 0);
        b = Vec2(0, 1);
        return true;
    }
    const SkEntity *e = sketch().find(lineId);
    if(!e || e->type != SkType::Line) return false;
    a = sketch().pointPos(e->a);
    b = sketch().pointPos(e->b);
    return true;
}

std::vector<Vec2> SketchEditor::polyline(const SkEntity &e) const {
    const cad::Sketch &s = sketch();
    std::vector<Vec2> out;
    switch(e.type) {
    case SkType::Point:
        break;
    case SkType::Line:
        out = {s.pointPos(e.a), s.pointPos(e.b)};
        break;
    case SkType::Circle: {
        const Vec2 c = s.pointPos(e.a);
        for(int i = 0; i <= kCircleSegments; ++i) {
            const double t = 2 * cad::kPi * i / kCircleSegments;
            out.push_back(c + Vec2(std::cos(t), std::sin(t)) * e.r);
        }
        break;
    }
    case SkType::Arc: {
        const Vec2 c = s.pointPos(e.a), st = s.pointPos(e.b), en = s.pointPos(e.c);
        const double r = distance(c, st), a0 = (st - c).angle(), sw = arcSweep(c, st, en);
        const int n = std::max(6, int(std::ceil(sw / (2 * cad::kPi) * kCircleSegments)));
        for(int i = 0; i <= n; ++i) {
            const double t = a0 + sw * i / n;
            out.push_back(c + Vec2(std::cos(t), std::sin(t)) * r);
        }
        break;
    }
    }
    return out;
}

// --- editing -----------------------------------------------------------------------

namespace {

std::shared_ptr<cad::ParamTable> paramsFor(const SketchEditor::ParamProvider &provider, const cad::SketchFeature &base,
                                           const cad::Sketch &s) {
    if(!provider) return nullptr;
    cad::SketchFeature f = base;
    f.sketch = s;
    return provider(f);
}

cad::SolveOutcome solveWith(cad::Sketch &s, const std::shared_ptr<cad::ParamTable> &params,
                            const cad::SolveOptions &options) {
    return cad::solveSketch(
        s,
        [&](const std::string &name, double &v) {
            if(!params) return false;
            const cad::ParamValue *pv = params->find(name);
            if(!pv || !pv->ok) return false;
            v = pv->value;
            return true;
        },
        options);
}

} // namespace

void SketchEditor::solve(const std::vector<int> &dragged, bool analyse) {
    m_params = paramsFor(m_paramProvider, *m_feature, sketch());
    cad::SolveOptions o;
    o.dragged = dragged;
    o.computeFreeEntities = analyse;
    cad::Sketch work = sketch();
    cad::SolveOutcome r = solveWith(work, m_params, o);
    if(r.ok) sketch() = std::move(work);
    if(!analyse) r.freeEntities = m_solve.freeEntities; // keep the colours while dragging
    m_solve = std::move(r);
    rebuildProfiles();
}

cad::EvalResult SketchEditor::evaluate(const std::string &expr, cad::ValueKind kind) const {
    if(m_params) return m_params->evaluateExpression(expr, kind);
    return cad::evaluate(expr, kind);
}

cad::SolveOutcome SketchEditor::trySolve(cad::Sketch &s) const {
    cad::SolveOptions o;
    o.computeFreeEntities = false;
    return solveWith(s, paramsFor(m_paramProvider, *m_feature, s), o);
}

bool SketchEditor::edit(const QString &label, const std::function<void(cad::Sketch &)> &fn,
                        bool rejectIfOverConstrained) {
    const cad::Sketch before = sketch();
    const bool wasOver = !m_solve.ok || m_solve.redundant;
    fn(sketch());
    solve();
    if(rejectIfOverConstrained && (!m_solve.ok || (m_solve.redundant && !wasOver))) {
        const QString why = m_solve.ok ? tr("it would over-constrain the sketch") : QString::fromStdString(m_solve.message);
        sketch() = before;
        solve();
        emit message(tr("%1 not applied: %2.").arg(label, why));
        emitChanged();
        return false;
    }
    m_undo.push_back({label, before});
    if(m_undo.size() > 500) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_modified = true;
    pruneSelection();
    emitChanged();
    return true;
}

bool SketchEditor::commit(const QString &label, cad::Sketch work, const std::vector<PendingConstraint> &extra,
                          std::vector<int> *added) {
    const bool wasOver = !m_solve.ok || m_solve.redundant;
    {
        cad::Sketch probe = work;
        const cad::SolveOutcome r = trySolve(probe);
        if(!r.ok) {
            emit message(tr("%1 not applied: %2.").arg(label, QString::fromStdString(r.message)));
            return false;
        }
        work = std::move(probe);
    }
    for(const PendingConstraint &pc : extra) {
        cad::Sketch trial = work;
        const int id = trial.addConstraint(pc.type, pc.e1, pc.e2, pc.e3);
        SkConstraint *c = trial.findConstraint(id);
        if(cad::isDimension(pc.type)) {
            c->param = m_allocate ? m_allocate() : std::string();
            c->expr = pc.expr;
            c->label = pc.label;
            c->supplementary = pc.supplementary;
        }
        const cad::SolveOutcome r = trySolve(trial);
        if(r.ok && (!r.redundant || wasOver)) {
            work = std::move(trial);
            if(added) added->push_back(id);
        } else if(added) {
            added->push_back(0);
        }
    }
    m_undo.push_back({label, sketch()});
    if(m_undo.size() > 500) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_modified = true;
    sketch() = std::move(work);
    solve();
    pruneSelection();
    emitChanged();
    return true;
}

void SketchEditor::setSketch(const cad::Sketch &s) {
    sketch() = s;
    solve();
    pruneSelection();
    emitChanged();
}

int SketchEditor::addDimension(SkCon type, int e1, int e2, Vec2 label, bool supplementary, bool *driven) {
    cad::Sketch work = sketch();
    const int id = work.addConstraint(type, e1, e2);
    SkConstraint *c = work.findConstraint(id);
    c->label = label;
    c->supplementary = supplementary;
    const double v = measuredValue(*c);
    c->param = m_allocate ? m_allocate() : std::string();
    c->expr = formatExpression(v, kindOf(type));
    const bool wasOver = !m_solve.ok || m_solve.redundant;
    cad::Sketch trial = work;
    const cad::SolveOutcome r = trySolve(trial);
    const bool isDriven = c->param.empty() || !r.ok || (r.redundant && !wasOver);
    if(isDriven) {
        c->driven = true;
        c->param.clear();
        c->expr.clear();
    }
    edit(isDriven ? tr("Driven Dimension") : tr("Dimension"), [&](cad::Sketch &s) { s = work; }, false);
    if(driven) *driven = isDriven;
    if(isDriven)
        emit message(tr("That dimension would over-constrain the sketch, so it was added as a driven (reference) dimension."));
    return id;
}

bool SketchEditor::setDimensionExpression(int constraintId, const QString &text, QString *error) {
    auto fail = [&](const QString &why) {
        if(error) *error = why;
        return false;
    };
    const SkConstraint *old = sketch().findConstraint(constraintId);
    if(!old || !cad::isDimension(old->type)) return fail(tr("not a dimension"));
    const std::string expr = text.trimmed().toStdString();
    if(expr.empty()) return fail(tr("enter a value"));
    cad::Sketch work = sketch();
    SkConstraint *c = work.findConstraint(constraintId);
    if(c->param.empty()) c->param = m_allocate ? m_allocate() : std::string();
    if(c->param.empty()) return fail(tr("no parameter name available"));
    c->driven = false;
    c->expr = expr;
    const auto table = paramsFor(m_paramProvider, *m_feature, work);
    const cad::ParamValue *pv = table ? table->find(c->param) : nullptr;
    if(!pv || !pv->ok) return fail(pv ? QString::fromStdString(pv->error) : tr("invalid value"));
    const bool mayBeZero = c->type == SkCon::HDistance || c->type == SkCon::VDistance ||
                           c->type == SkCon::PointLineDistance || c->type == SkCon::Angle;
    if(pv->value < 0 || (!mayBeZero && pv->value < 1e-9)) return fail(tr("the value must be positive"));
    if(c->type == SkCon::Angle && pv->value > cad::kPi + 1e-9) return fail(tr("angles must be between 0 and 180 deg"));
    if(!edit(tr("Edit Dimension"), [&](cad::Sketch &s) { s = work; }, true))
        return fail(tr("the sketch cannot be solved with that value"));
    return true;
}

void SketchEditor::deleteSelection() {
    std::set<int> entities;
    for(int id : selectedEntities)
        if(sketch().find(id)) entities.insert(id);
    const std::set<int> constraints = selectedConstraints;
    if(entities.empty() && constraints.empty()) return;
    edit(tr("Delete"),
         [&](cad::Sketch &s) {
             for(int c : constraints) s.removeConstraint(c);
             for(int e : entities) s.removeEntity(e);
         },
         false);
    clearSelection();
}

void SketchEditor::toggleConstruction() {
    std::vector<int> ids;
    for(int id : selectedEntities)
        if(sketch().find(id)) ids.push_back(id);
    if(ids.empty()) {
        emit message(tr("Select sketch curves, then toggle construction (X)."));
        return;
    }
    edit(tr("Construction"),
         [&](cad::Sketch &s) {
             bool anyNormal = false;
             for(int id : ids) anyNormal |= !s.find(id)->construction;
             for(int id : ids) s.find(id)->construction = anyNormal;
         },
         false);
}

bool SketchEditor::undo() {
    if(m_undo.empty()) return false;
    Snapshot snap = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back({snap.label, sketch()});
    sketch() = std::move(snap.sketch);
    m_modified = true;
    solve();
    pruneSelection();
    emit message(tr("Undo %1").arg(snap.label));
    emitChanged();
    return true;
}

bool SketchEditor::redo() {
    if(m_redo.empty()) return false;
    Snapshot snap = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back({snap.label, sketch()});
    sketch() = std::move(snap.sketch);
    m_modified = true;
    solve();
    pruneSelection();
    emit message(tr("Redo %1").arg(snap.label));
    emitChanged();
    return true;
}

void SketchEditor::rebuildProfiles() {
    m_profiles = cad::buildProfiles(cad::sketchCurves(sketch())).profiles;
    m_profileTriangles.assign(m_profiles.size(), {});
    for(size_t i = 0; i < m_profiles.size(); ++i) m_profileTriangles[i] = triangulateProfile(m_profiles[i], m_frame);
    for(auto it = selectedProfiles.begin(); it != selectedProfiles.end();)
        it = *it >= int(m_profiles.size()) ? selectedProfiles.erase(it) : std::next(it);
}

void SketchEditor::pruneSelection() {
    const cad::Sketch &s = sketch();
    for(auto it = selectedEntities.begin(); it != selectedEntities.end();)
        it = (*it > 0 && !s.find(*it)) ? selectedEntities.erase(it) : std::next(it);
    for(auto it = selectedConstraints.begin(); it != selectedConstraints.end();)
        it = !s.findConstraint(*it) ? selectedConstraints.erase(it) : std::next(it);
    for(auto it = selectedProfiles.begin(); it != selectedProfiles.end();)
        it = *it >= int(m_profiles.size()) ? selectedProfiles.erase(it) : std::next(it);
    if((hover.kind == Kind::Point || hover.kind == Kind::Curve) && !s.find(hover.id)) hover = {};
    if((hover.kind == Kind::Dimension || hover.kind == Kind::Constraint) && !s.findConstraint(hover.id)) hover = {};
    if(hover.kind == Kind::Profile && hover.id >= int(m_profiles.size())) hover = {};
}

void SketchEditor::refreshView() const { m_viewport->refreshOverlay(); }

void SketchEditor::emitChanged() {
    refreshView();
    emit changed();
}

void SketchEditor::clearPreview() {
    previewLines.clear();
    previewConstruction.clear();
    previewPoints.clear();
    previewSnap.reset();
    refreshView();
}

// --- dimensions ----------------------------------------------------------------------

double SketchEditor::measuredValue(const SkConstraint &c) const {
    const cad::Sketch &s = sketch();
    switch(c.type) {
    case SkCon::Distance: {
        Vec2 a, b;
        if(c.e2 == 0 && lineEnds(c.e1, a, b)) return distance(a, b);
        return distance(posOf(c.e1), posOf(c.e2));
    }
    case SkCon::HDistance: return std::fabs(posOf(c.e2).x - posOf(c.e1).x);
    case SkCon::VDistance: return std::fabs(posOf(c.e2).y - posOf(c.e1).y);
    case SkCon::PointLineDistance: {
        int pt = c.e1, line = c.e2;
        Vec2 a, b;
        if(!lineEnds(line, a, b)) {
            std::swap(pt, line);
            if(!lineEnds(line, a, b)) return 0.0;
        }
        const Vec2 d = (b - a).normalized();
        return std::fabs(d.cross(posOf(pt) - a));
    }
    case SkCon::Radius:
    case SkCon::Diameter: {
        const SkEntity *e = s.find(c.e1);
        if(!e || (e->type != SkType::Circle && e->type != SkType::Arc)) return 0.0;
        const double r = e->type == SkType::Circle ? e->r : s.arcRadius(*e);
        return c.type == SkCon::Radius ? r : 2 * r;
    }
    case SkCon::Angle: {
        Vec2 a1, b1, a2, b2;
        if(!lineEnds(c.e1, a1, b1) || !lineEnds(c.e2, a2, b2)) return 0.0;
        const Vec2 d1 = (b1 - a1).normalized(), d2 = (b2 - a2).normalized();
        const double ang = std::acos(std::clamp(d1.dot(d2), -1.0, 1.0));
        return c.supplementary ? cad::kPi - ang : ang;
    }
    default: return 0.0;
    }
}

double SketchEditor::dimensionValue(const SkConstraint &c) const {
    if(!c.driven && m_params) {
        const cad::ParamValue *pv = m_params->find(c.param);
        if(pv && pv->ok) return std::fabs(pv->value);
    }
    return measuredValue(c);
}

QString SketchEditor::dimensionText(const SkConstraint &c) const {
    const double v = dimensionValue(c);
    QString text = c.type == SkCon::Angle ? QString::number(v * 180.0 / cad::kPi, 'f', 1) + QChar(0x00B0)
                                          : QString::number(v, 'f', 2);
    if(c.type == SkCon::Radius) text.prepend(QLatin1Char('R'));
    if(c.type == SkCon::Diameter) text.prepend(QChar(0x2300));
    std::set<std::string> names;
    if(!c.driven && cad::referencedNames(c.expr, names) && !names.empty()) text.prepend(QStringLiteral("fx: "));
    if(c.driven) text = QLatin1Char('(') + text + QLatin1Char(')');
    return text;
}

Vec2 SketchEditor::dimensionAnchor(const SkConstraint &c) const {
    switch(c.type) {
    case SkCon::Distance: {
        Vec2 a, b;
        if(c.e2 == 0 && lineEnds(c.e1, a, b)) return (a + b) * 0.5;
        return (posOf(c.e1) + posOf(c.e2)) * 0.5;
    }
    case SkCon::HDistance:
    case SkCon::VDistance: return (posOf(c.e1) + posOf(c.e2)) * 0.5;
    case SkCon::PointLineDistance: {
        int pt = c.e1, line = c.e2;
        Vec2 a, b;
        if(!lineEnds(line, a, b)) {
            std::swap(pt, line);
            if(!lineEnds(line, a, b)) return posOf(pt);
        }
        const Vec2 p = posOf(pt), d = (b - a).normalized();
        const Vec2 foot = a + d * (p - a).dot(d);
        return (p + foot) * 0.5;
    }
    case SkCon::Radius:
    case SkCon::Diameter: {
        const SkEntity *e = sketch().find(c.e1);
        return e ? sketch().pointPos(e->a) : Vec2();
    }
    case SkCon::Angle: {
        Vec2 a1, b1, a2, b2;
        if(!lineEnds(c.e1, a1, b1) || !lineEnds(c.e2, a2, b2)) return Vec2();
        const Vec2 d1 = b1 - a1, d2 = b2 - a2;
        const double den = d1.cross(d2);
        if(std::fabs(den) < 1e-12) return ((a1 + b1) * 0.5 + (a2 + b2) * 0.5) * 0.5;
        const double t = (a2 - a1).cross(d2) / den;
        return a1 + d1 * t;
    }
    default: return Vec2();
    }
}

std::optional<QRectF> SketchEditor::dimensionRect(int constraintId) const {
    auto it = m_dimensionRects.find(constraintId);
    if(it == m_dimensionRects.end()) return std::nullopt;
    return it->second;
}

std::string SketchEditor::formatExpression(double value, cad::ValueKind kind) {
    if(kind == cad::ValueKind::Angle) return trimNumber(value * 180.0 / cad::kPi, 3) + " deg";
    return trimNumber(value, 3) + " mm";
}

// --- picking ---------------------------------------------------------------------------

bool SketchEditor::curveNear(const SkEntity &e, QPointF px, double tol, double *dist) const {
    const Projector P(m_viewport);
    const auto pl = polyline(e);
    double best = tol;
    bool found = false;
    QPointF prev;
    for(size_t k = 0; k < pl.size(); ++k) {
        const QPointF cur = P(toWorld(pl[k]));
        if(k > 0) {
            const double d = segDist(px, prev, cur);
            if(d < best) {
                best = d;
                found = true;
            }
        }
        prev = cur;
    }
    if(dist) *dist = best;
    return found;
}

SketchHit SketchEditor::hitTest(QPointF px, unsigned filter) const {
    const Projector P(m_viewport);
    const cad::Sketch &s = sketch();
    if(filter & HitDimensions)
        for(const auto &[id, r] : m_dimensionRects)
            if(r.adjusted(-2, -2, 2, 2).contains(px)) return {Kind::Dimension, id};
    if(filter & HitConstraints)
        for(const auto &[id, r] : m_glyphRects)
            if(r.contains(px)) return {Kind::Constraint, id};
    if(filter & HitPoints) {
        double best = kPointTol;
        int bestId = 0;
        for(const auto &e : s.entities) {
            if(e.type != SkType::Point) continue;
            const double d = pxDist(P(toWorld({e.x, e.y})), px);
            if(d < best) {
                best = d;
                bestId = e.id;
            }
        }
        if(bestId) return {Kind::Point, bestId};
    }
    if((filter & HitOrigin) && pxDist(P(toWorld({0, 0})), px) < kPointTol) return {Kind::Origin, cad::kSketchOrigin};
    if(filter & HitCurves) {
        double best = kCurveTol;
        int bestId = 0;
        for(const auto &e : s.entities) {
            if(!e.isCurve()) continue;
            double d;
            if(curveNear(e, px, best, &d)) {
                best = d;
                bestId = e.id;
            }
        }
        if(bestId) return {Kind::Curve, bestId};
    }
    if(filter & HitAxes) {
        const double ext = std::max(10.0, double(m_viewport->content().gridExtent));
        const double dx = segDist(px, P(toWorld({-ext, 0})), P(toWorld({ext, 0})));
        const double dy = segDist(px, P(toWorld({0, -ext})), P(toWorld({0, ext})));
        if(std::min(dx, dy) < kCurveTol) return {Kind::Axis, dx <= dy ? cad::kSketchXAxis : cad::kSketchYAxis};
    }
    if((filter & HitProfiles) && m_options.profiles) {
        if(const auto q = toSketch(px)) {
            int best = -1;
            double bestArea = 0;
            for(size_t i = 0; i < m_profiles.size(); ++i) {
                if(!m_profiles[i].contains(*q)) continue;
                const double a = std::fabs(m_profiles[i].area);
                if(best < 0 || a < bestArea) {
                    best = int(i);
                    bestArea = a;
                }
            }
            if(best >= 0) return {Kind::Profile, best};
        }
    }
    return {};
}

SketchSnap SketchEditor::snap(QPointF px, const std::set<int> &exclude, bool allowCurves) const {
    SketchSnap out;
    const auto q = toSketch(px);
    if(!q) return out;
    out.pos = *q;
    const Projector P(m_viewport);
    const cad::Sketch &s = sketch();
    auto screenDist = [&](Vec2 v) { return pxDist(P(toWorld(v)), px); };

    // Existing points first, then the origin.
    double best = kSnapTol;
    for(const auto &e : s.entities) {
        if(e.type != SkType::Point || exclude.count(e.id)) continue;
        const double d = screenDist({e.x, e.y});
        if(d < best) {
            best = d;
            out = {SketchSnap::Kind::Point, {e.x, e.y}, e.id};
        }
    }
    if(out.kind == SketchSnap::Kind::Point) return out;
    if(screenDist({0, 0}) < kSnapTol) return {SketchSnap::Kind::Origin, {0, 0}, cad::kSketchOrigin};

    if(allowCurves) {
        auto usesExcluded = [&](const SkEntity &e) {
            return exclude.count(e.a) || exclude.count(e.b) || exclude.count(e.c);
        };
        // Midpoints of lines, then quadrants of circles and arcs.
        best = kSnapTol;
        SketchSnap mid;
        for(const auto &e : s.entities) {
            if(e.type != SkType::Line || usesExcluded(e)) continue;
            const Vec2 m = (s.pointPos(e.a) + s.pointPos(e.b)) * 0.5;
            const double d = screenDist(m);
            if(d < best) {
                best = d;
                mid = {SketchSnap::Kind::Midpoint, m, e.id};
            }
        }
        if(mid.kind != SketchSnap::Kind::None) return mid;
        best = kSnapTol;
        SketchSnap quad;
        for(const auto &e : s.entities) {
            if((e.type != SkType::Circle && e.type != SkType::Arc) || usesExcluded(e)) continue;
            const Vec2 c = s.pointPos(e.a);
            const double r = e.type == SkType::Circle ? e.r : s.arcRadius(e);
            for(int k = 0; k < 4; ++k) {
                const double ang = k * cad::kPi / 2;
                if(e.type == SkType::Arc) {
                    const Vec2 st = s.pointPos(e.b), en = s.pointPos(e.c);
                    if(cad::normAngle(ang - (st - c).angle()) > arcSweep(c, st, en)) continue;
                }
                const Vec2 v = c + Vec2(std::cos(ang), std::sin(ang)) * r;
                const double d = screenDist(v);
                if(d < best) {
                    best = d;
                    quad = {SketchSnap::Kind::Quadrant, v, e.id};
                }
            }
        }
        if(quad.kind != SketchSnap::Kind::None) return quad;

        // Nearest point on a curve.
        best = kCurveTol;
        SketchSnap on;
        for(const auto &e : s.entities) {
            if(!e.isCurve() || usesExcluded(e)) continue;
            Vec2 v;
            if(e.type == SkType::Line) {
                const Vec2 a = s.pointPos(e.a), b = s.pointPos(e.b), d = b - a;
                const double l2 = d.length2();
                if(l2 < 1e-18) continue;
                v = a + d * std::clamp((*q - a).dot(d) / l2, 0.0, 1.0);
            } else {
                const Vec2 c = s.pointPos(e.a);
                const double r = e.type == SkType::Circle ? e.r : s.arcRadius(e);
                Vec2 dir = *q - c;
                if(dir.length() < 1e-12) continue;
                dir = dir.normalized();
                if(e.type == SkType::Arc) {
                    const Vec2 st = s.pointPos(e.b), en = s.pointPos(e.c);
                    if(cad::normAngle(dir.angle() - (st - c).angle()) > arcSweep(c, st, en)) continue;
                }
                v = c + dir * r;
            }
            const double d = screenDist(v);
            if(d < best) {
                best = d;
                on = {SketchSnap::Kind::OnCurve, v, e.id};
            }
        }
        if(on.kind != SketchSnap::Kind::None) return on;

        // The sketch axes.
        const double dx = screenDist({q->x, 0}), dy = screenDist({0, q->y});
        if(std::min(dx, dy) < kCurveTol) {
            if(dx <= dy) return {SketchSnap::Kind::OnCurve, {q->x, 0}, cad::kSketchXAxis};
            return {SketchSnap::Kind::OnCurve, {0, q->y}, cad::kSketchYAxis};
        }
    }
    if(m_options.snapToGrid) {
        const double g = std::max(1e-6, double(m_viewport->content().gridMinor));
        out.kind = SketchSnap::Kind::Grid;
        out.pos = Vec2(std::round(q->x / g) * g, std::round(q->y / g) * g);
    }
    return out;
}

int SketchEditor::pointForSnap(cad::Sketch &s, const SketchSnap &snap, std::vector<PendingConstraint> &pending) const {
    switch(snap.kind) {
    case SketchSnap::Kind::Point:
        if(s.find(snap.entity)) return snap.entity;
        break;
    case SketchSnap::Kind::Origin: {
        const int p = s.addPoint(0, 0);
        pending.push_back({SkCon::Coincident, p, cad::kSketchOrigin});
        return p;
    }
    case SketchSnap::Kind::Midpoint: {
        const int p = s.addPoint(snap.pos.x, snap.pos.y);
        pending.push_back({SkCon::Midpoint, p, snap.entity});
        return p;
    }
    case SketchSnap::Kind::Quadrant:
    case SketchSnap::Kind::OnCurve: {
        const int p = s.addPoint(snap.pos.x, snap.pos.y);
        pending.push_back({SkCon::PointOnCurve, p, snap.entity});
        return p;
    }
    default:
        break;
    }
    return s.addPoint(snap.pos.x, snap.pos.y);
}

std::vector<int> SketchEditor::entitiesInRect(const QRectF &rect, bool crossing) const {
    const Projector P(m_viewport);
    const cad::Sketch &s = sketch();
    std::set<int> usedPoints;
    for(const auto &e : s.entities)
        if(e.isCurve())
            for(int p : {e.a, e.b, e.c})
                if(p) usedPoints.insert(p);
    const QLineF edges[4] = {{rect.topLeft(), rect.topRight()},
                             {rect.topRight(), rect.bottomRight()},
                             {rect.bottomRight(), rect.bottomLeft()},
                             {rect.bottomLeft(), rect.topLeft()}};
    std::vector<int> out;
    for(const auto &e : s.entities) {
        if(e.type == SkType::Point) {
            if(!usedPoints.count(e.id) && rect.contains(P(toWorld({e.x, e.y})))) out.push_back(e.id);
            continue;
        }
        std::vector<QPointF> pts;
        for(const Vec2 &v : polyline(e)) pts.push_back(P(toWorld(v)));
        bool all = !pts.empty(), any = false;
        for(const QPointF &p : pts) {
            const bool in = rect.contains(p);
            all &= in;
            any |= in;
        }
        if(crossing && !any) {
            for(size_t k = 0; k + 1 < pts.size() && !any; ++k)
                for(const QLineF &edge : edges)
                    if(QLineF(pts[k], pts[k + 1]).intersects(edge) == QLineF::BoundedIntersection) {
                        any = true;
                        break;
                    }
        }
        if(crossing ? any : all) out.push_back(e.id);
    }
    return out;
}

// --- selection ---------------------------------------------------------------------------

void SketchEditor::clearSelection() {
    if(selectedEntities.empty() && selectedConstraints.empty() && selectedProfiles.empty()) return;
    selectedEntities.clear();
    selectedConstraints.clear();
    selectedProfiles.clear();
    emitChanged();
}

void SketchEditor::select(const SketchHit &hit, bool additive) {
    if(!additive) {
        selectedEntities.clear();
        selectedConstraints.clear();
        selectedProfiles.clear();
    }
    auto toggle = [&](std::set<int> &set, int id) {
        if(additive && set.count(id)) set.erase(id);
        else set.insert(id);
    };
    switch(hit.kind) {
    case Kind::Point:
    case Kind::Curve:
    case Kind::Origin:
    case Kind::Axis: toggle(selectedEntities, hit.id); break;
    case Kind::Dimension:
    case Kind::Constraint: toggle(selectedConstraints, hit.id); break;
    case Kind::Profile: toggle(selectedProfiles, hit.id); break;
    case Kind::None: break;
    }
    emitChanged();
}

void SketchEditor::selectEntities(const std::vector<int> &ids, bool additive) {
    if(!additive) {
        selectedEntities.clear();
        selectedConstraints.clear();
        selectedProfiles.clear();
    }
    selectedEntities.insert(ids.begin(), ids.end());
    emitChanged();
}

std::vector<int> SketchEditor::connectedChain(int curveId) const {
    const cad::Sketch &s = sketch();
    std::vector<int> out;
    const SkEntity *start = s.find(curveId);
    if(!start || !start->isCurve()) return out;
    auto ends = [](const SkEntity &e) -> std::vector<int> {
        if(e.type == SkType::Line) return {e.a, e.b};
        if(e.type == SkType::Arc) return {e.b, e.c};
        return {};
    };
    std::set<int> seen{curveId};
    std::vector<int> todo{curveId};
    while(!todo.empty()) {
        const int id = todo.back();
        todo.pop_back();
        out.push_back(id);
        const SkEntity *e = s.find(id);
        for(int p : ends(*e))
            for(const auto &o : s.entities)
                if(o.isCurve() && !seen.count(o.id) && o.construction == start->construction) {
                    const auto oe = ends(o);
                    if(std::find(oe.begin(), oe.end(), p) != oe.end()) {
                        seen.insert(o.id);
                        todo.push_back(o.id);
                    }
                }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool SketchEditor::isSelected(const SketchHit &hit) const {
    switch(hit.kind) {
    case Kind::Point:
    case Kind::Curve:
    case Kind::Origin:
    case Kind::Axis: return selectedEntities.count(hit.id) > 0;
    case Kind::Dimension:
    case Kind::Constraint: return selectedConstraints.count(hit.id) > 0;
    case Kind::Profile: return selectedProfiles.count(hit.id) > 0;
    case Kind::None: return false;
    }
    return false;
}

QString SketchEditor::selectionStats() const {
    using cad::Measurement;
    using cad::MeasureUnit;
    const cad::Sketch &s = sketch();
    std::vector<int> ents;
    for(int id : selectedEntities)
        if(s.find(id)) ents.push_back(id);
    const size_t total = ents.size() + selectedConstraints.size() + selectedProfiles.size();
    std::vector<Measurement> m;
    auto text = [](const std::string &t) { return Measurement{"", 0.0, MeasureUnit::Text, t}; };
    if(total == 0) return {};
    if(total == 1 && ents.size() == 1) {
        const SkEntity *e = s.find(ents[0]);
        static const char *names[] = {"Point", "Line", "Circle", "Arc"};
        m.push_back(text(std::string(e->construction ? "Construction " : "") + names[int(e->type)]));
        const auto more = cad::measureSketchEntity(s, ents[0]);
        m.insert(m.end(), more.begin(), more.end());
    } else if(total == 1 && selectedConstraints.size() == 1) {
        const SkConstraint *c = s.findConstraint(*selectedConstraints.begin());
        if(c && cad::isDimension(c->type)) {
            m.push_back(text(c->driven ? std::string("Driven dimension") : c->param));
            m.push_back({"Value", dimensionValue(*c), c->type == SkCon::Angle ? MeasureUnit::Angle : MeasureUnit::Length, {}});
            std::set<std::string> names;
            if(!c->driven && cad::referencedNames(c->expr, names) && !names.empty())
                m.push_back({"Expression", 0.0, MeasureUnit::Text, c->expr});
        } else if(c) {
            m.push_back(text(std::string(cad::toString(c->type)) + " constraint"));
        }
    } else if(selectedProfiles.size() == total) {
        double area = 0.0;
        for(int i : selectedProfiles) area += std::fabs(m_profiles[size_t(i)].area);
        m.push_back(text(std::to_string(total) + (total == 1 ? " profile" : " profiles")));
        m.push_back({"Area", area, MeasureUnit::Area, {}});
    } else if(ents.size() == total) {
        bool allPoints = true, allCurves = true;
        double length = 0.0;
        for(int id : ents) {
            const SkEntity *e = s.find(id);
            allPoints &= e->type == SkType::Point;
            allCurves &= e->isCurve();
            if(e->type == SkType::Line) length += distance(s.pointPos(e->a), s.pointPos(e->b));
            if(e->type == SkType::Circle) length += 2 * cad::kPi * e->r;
            if(e->type == SkType::Arc)
                length += s.arcRadius(*e) * arcSweep(s.pointPos(e->a), s.pointPos(e->b), s.pointPos(e->c));
        }
        if(allPoints && ents.size() == 2) {
            const Vec2 a = s.pointPos(ents[0]), b = s.pointPos(ents[1]);
            m.push_back({"Distance", distance(a, b), MeasureUnit::Length, {}});
            m.push_back({"dX", std::fabs(b.x - a.x), MeasureUnit::Length, {}});
            m.push_back({"dY", std::fabs(b.y - a.y), MeasureUnit::Length, {}});
        } else if(allCurves) {
            m.push_back(text(std::to_string(total) + " curves"));
            m.push_back({"Total length", length, MeasureUnit::Length, {}});
        } else {
            m.push_back(text(std::to_string(total) + " selected"));
        }
    } else {
        m.push_back(text(std::to_string(total) + " selected"));
    }
    return QString::fromStdString(cad::formatMeasurements(m));
}

// --- dragging -------------------------------------------------------------------------------

bool SketchEditor::beginDrag(const SketchHit &hit, QPointF px) {
    const auto q = toSketch(px);
    if(!q) return false;
    const cad::Sketch &s = sketch();
    Drag d;
    d.hit = hit;
    d.start = *q;
    d.before = s;
    d.lastGood = s;
    switch(hit.kind) {
    case Kind::Point:
        if(!s.find(hit.id)) return false;
        d.origin[hit.id] = s.pointPos(hit.id);
        break;
    case Kind::Curve: {
        const SkEntity *e = s.find(hit.id);
        if(!e) return false;
        if(e->type == SkType::Line || e->type == SkType::Arc)
            for(int p : {e->a, e->b, e->c})
                if(p) d.origin[p] = s.pointPos(p);
        break;
    }
    case Kind::Dimension: {
        const SkConstraint *c = s.findConstraint(hit.id);
        if(!c) return false;
        d.labelStart = c->label;
        break;
    }
    default:
        return false;
    }
    d.active = true;
    m_drag = std::move(d);
    return true;
}

void SketchEditor::dragTo(QPointF px) {
    if(!m_drag.active) return;
    const auto q = toSketch(px);
    if(!q) return;
    const Vec2 delta = *q - m_drag.start;
    m_drag.moved = true;
    cad::Sketch &s = sketch();
    if(m_drag.hit.kind == Kind::Dimension) {
        if(SkConstraint *c = s.findConstraint(m_drag.hit.id)) c->label = m_drag.labelStart + delta;
        refreshView();
        return;
    }
    std::vector<int> dragged;
    for(const auto &[id, p0] : m_drag.origin) {
        if(SkEntity *e = s.find(id)) {
            e->x = p0.x + delta.x;
            e->y = p0.y + delta.y;
            dragged.push_back(id);
        }
    }
    if(m_drag.hit.kind == Kind::Curve) {
        SkEntity *e = s.find(m_drag.hit.id);
        if(e && e->type == SkType::Circle) e->r = std::max(distance(s.pointPos(e->a), *q), 1e-3);
    }
    solve(dragged, false);
    if(!m_solve.ok) {
        sketch() = m_drag.lastGood;
        solve({}, false);
    } else {
        m_drag.lastGood = sketch();
    }
    emitChanged();
}

void SketchEditor::endDrag() {
    if(!m_drag.active) return;
    const bool dimension = m_drag.hit.kind == Kind::Dimension;
    cad::Sketch before = std::move(m_drag.before);
    const bool moved = m_drag.moved;
    m_drag = Drag();
    if(moved && before.toJson() != sketch().toJson()) {
        m_undo.push_back({dimension ? tr("Move Dimension") : tr("Drag"), std::move(before)});
        m_redo.clear();
        m_modified = true;
    }
    solve();
    emitChanged();
}

// --- drawing --------------------------------------------------------------------------------

void SketchEditor::contribute(RenderScene &scene) const {
    scene.gridFrame = frameMatrix();
    if(!m_options.grid) scene.grid = false;
    const cad::Sketch &s = sketch();

    if(m_options.profiles) {
        TriangleBatch normal, hovered, selected;
        normal.color = kProfileColor;
        hovered.color = kProfileHoverColor;
        selected.color = kProfileSelectedColor;
        for(size_t i = 0; i < m_profileTriangles.size(); ++i) {
            TriangleBatch &b = selectedProfiles.count(int(i))                       ? selected
                               : (hover.kind == Kind::Profile && hover.id == int(i)) ? hovered
                                                                                     : normal;
            b.triangles.insert(b.triangles.end(), m_profileTriangles[i].begin(), m_profileTriangles[i].end());
        }
        for(TriangleBatch *b : {&normal, &hovered, &selected})
            if(!b->triangles.empty()) scene.triangles.push_back(*b);
    }

    // Emphasis sets.
    std::set<int> failed, hot, selected;
    for(int cid : m_solve.failed)
        if(const SkConstraint *c = s.findConstraint(cid))
            for(int e : {c->e1, c->e2, c->e3}) failed.insert(e);
    const std::set<int> freeEnts(m_solve.freeEntities.begin(), m_solve.freeEntities.end());
    if(hover.isGeometry()) hot.insert(hover.id);
    if(hover.kind == Kind::Dimension || hover.kind == Kind::Constraint)
        if(const SkConstraint *c = s.findConstraint(hover.id)) hot.insert({c->e1, c->e2, c->e3});
    selected = selectedEntities;
    for(int cid : selectedConstraints)
        if(const SkConstraint *c = s.findConstraint(cid)) selected.insert({c->e1, c->e2, c->e3});

    auto batch = [](const QColor &c, float width) {
        LineBatch b;
        b.color = c;
        b.width = width;
        return b;
    };
    LineBatch construction = batch(kConstructionColor, 1.3f), fixed = batch(kFixedColor, 1.7f),
              freeB = batch(kFreeColor, 1.7f), failedB = batch(kFailedColor, 2.0f), hov = batch(kHoverColor, 2.8f),
              sel = batch(kSelectedColor, 2.8f);
    // Construction geometry is dashed along whole curves (about 6 px on, 5 px off).
    const double dashUnits = [&] {
        const auto c = toSketch(QPointF(m_viewport->width() / 2.0, m_viewport->height() / 2.0));
        return sketchUnitsPerPixel(c.value_or(Vec2())) * 6.0;
    }();
    auto addPolyline = [&](LineBatch &b, const std::vector<Vec2> &pl, bool dashed) {
        if(pl.size() < 2) return;
        if(!dashed) {
            for(size_t k = 0; k + 1 < pl.size(); ++k) {
                b.segments.push_back(toWorld(pl[k]));
                b.segments.push_back(toWorld(pl[k + 1]));
            }
            return;
        }
        // Dash k covers arc length [k * period, k * period + on] of the whole polyline.
        std::vector<double> cum(pl.size(), 0.0);
        for(size_t k = 1; k < pl.size(); ++k) cum[k] = cum[k - 1] + distance(pl[k - 1], pl[k]);
        const double total = cum.back();
        if(total < 1e-12) return;
        const double on = std::max({dashUnits, total / 300.0, 1e-9}), period = on * 11.0 / 6.0;
        size_t seg = 0; // segment containing the current dash start
        auto pointAt = [&](double len, size_t from) {
            while(from + 2 < pl.size() && cum[from + 1] < len) ++from;
            const double segLen = cum[from + 1] - cum[from];
            const double t = segLen > 1e-15 ? std::clamp((len - cum[from]) / segLen, 0.0, 1.0) : 0.0;
            return std::make_pair(pl[from] + (pl[from + 1] - pl[from]) * t, from);
        };
        const int dashes = int(std::ceil(total / period));
        for(int k = 0; k < dashes; ++k) {
            const double s0 = k * period, s1 = std::min(total, s0 + on);
            if(s1 <= s0) break;
            auto [start, i0] = pointAt(s0, seg);
            seg = i0;
            auto [end, i1] = pointAt(s1, seg);
            Vec2 prev = start;
            for(size_t j = i0 + 1; j <= i1; ++j) { // corners inside the dash
                b.segments.push_back(toWorld(prev));
                b.segments.push_back(toWorld(pl[j]));
                prev = pl[j];
            }
            b.segments.push_back(toWorld(prev));
            b.segments.push_back(toWorld(end));
        }
    };
    for(const auto &e : s.entities) {
        if(!e.isCurve()) continue;
        LineBatch &base = e.construction   ? construction
                          : failed.count(e.id) ? failedB
                          : freeEnts.count(e.id) ? freeB
                                                 : fixed;
        const auto pl = polyline(e);
        addPolyline(base, pl, e.construction);
        if(selected.count(e.id)) addPolyline(sel, pl, e.construction);
        else if(hot.count(e.id)) addPolyline(hov, pl, e.construction);
    }
    // Highlighted sketch axes.
    const float ext = std::max(10.0f, m_viewport->content().gridExtent);
    for(int axis : {cad::kSketchXAxis, cad::kSketchYAxis}) {
        LineBatch *b = selected.count(axis) ? &sel : hot.count(axis) ? &hov : nullptr;
        if(!b) continue;
        const Vec2 d = axis == cad::kSketchXAxis ? Vec2(1, 0) : Vec2(0, 1);
        b->segments.push_back(toWorld(d * -ext));
        b->segments.push_back(toWorld(d * ext));
    }
    for(LineBatch *b : {&construction, &fixed, &freeB, &failedB, &hov, &sel})
        if(!b->segments.empty()) scene.lines.push_back(*b);

    // Points: white with a coloured ring.
    auto points = [](const QColor &fill, const QColor &ring, float size) {
        PointBatch b;
        b.color = fill;
        b.outline = ring;
        b.size = size;
        return b;
    };
    PointBatch fixedP = points(Qt::white, kFixedColor, 7.0f), freeP = points(Qt::white, kFreeColor, 7.0f),
               failedP = points(Qt::white, kFailedColor, 7.5f), hovP = points(kHoverColor, kHoverColor.darker(130), 9.0f),
               selP = points(kSelectedColor, kSelectedColor.darker(140), 9.0f);
    for(const auto &e : s.entities) {
        if(e.type != SkType::Point) continue;
        const QVector3D w = toWorld({e.x, e.y});
        if(selected.count(e.id)) selP.points.push_back(w);
        else if(hot.count(e.id)) hovP.points.push_back(w);
        else if(!m_options.points) continue;
        else if(failed.count(e.id)) failedP.points.push_back(w);
        else if(freeEnts.count(e.id)) freeP.points.push_back(w);
        else fixedP.points.push_back(w);
    }
    // The sketch origin.
    PointBatch origin = points(QColor(70, 78, 90), Qt::white, 7.0f);
    if(selected.count(cad::kSketchOrigin)) origin = points(kSelectedColor, Qt::white, 9.0f);
    else if(hot.count(cad::kSketchOrigin)) origin = points(kHoverColor, Qt::white, 9.0f);
    origin.points.push_back(toWorld({0, 0}));
    for(PointBatch *b : {&origin, &fixedP, &freeP, &failedP, &hovP, &selP})
        if(!b->points.empty()) scene.points.push_back(*b);

    // Geometry being drawn by the active tool.
    if(!previewLines.empty() || !previewConstruction.empty()) {
        LineBatch solid = batch(kFreeColor, 1.7f), dashed = batch(kConstructionColor, 1.3f);
        for(const auto &[a, b] : previewLines) solid.segments.insert(solid.segments.end(), {toWorld(a), toWorld(b)});
        for(const auto &[a, b] : previewConstruction) addPolyline(dashed, {a, b}, true);
        for(LineBatch *b : {&dashed, &solid})
            if(!b->segments.empty()) scene.lines.push_back(*b);
    }
    if(!previewPoints.empty()) {
        PointBatch pb = points(Qt::white, kFreeColor, 7.0f);
        for(const Vec2 &v : previewPoints) pb.points.push_back(toWorld(v));
        scene.points.push_back(pb);
    }
    if(previewSnap && previewSnap->attaches()) {
        PointBatch ring = points(QColor(0, 0, 0, 0), kSelectedColor, 14.0f);
        ring.depthTest = false;
        ring.points.push_back(toWorld(previewSnap->pos));
        scene.points.push_back(ring);
    }
}

void SketchEditor::paintOverlay(QPainter &p) {
    m_dimensionRects.clear();
    m_glyphRects.clear();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if(m_options.dimensions)
        for(const auto &c : sketch().constraints)
            if(cad::isDimension(c.type)) paintDimension(p, c);
    if(m_options.constraints) paintGlyphs(p);
    p.restore();
}

void SketchEditor::paintDimension(QPainter &p, const SkConstraint &c) {
    const Projector P(m_viewport);
    auto S = [&](Vec2 v) { return P(toWorld(v)); };
    const cad::Sketch &s = sketch();
    const bool isSel = selectedConstraints.count(c.id) > 0;
    const bool isHot = (hover.kind == Kind::Dimension && hover.id == c.id);
    const QColor col = contains(m_solve.failed, c.id) ? kFailedColor
                       : isSel                         ? kSelectedColor.darker(115)
                       : isHot                         ? kFreeColor
                       : c.driven                      ? kDrivenColor
                                                       : kDimensionColor;
    p.setPen(QPen(col, 1.0));
    p.setBrush(Qt::NoBrush);
    const Vec2 anchor = dimensionAnchor(c);
    const Vec2 L = anchor + c.label;
    const QPointF sL = S(L);

    auto extension = [&](QPointF from, QPointF to) {
        const QPointF v = to - from;
        const double l = std::hypot(v.x(), v.y());
        if(l < 3.0) return;
        p.drawLine(from + v / l * 3.0, to + v / l * 5.0);
    };
    auto linear = [&](Vec2 P1, Vec2 P2, Vec2 dir) {
        const Vec2 n = dir.perp();
        const Vec2 A = P1 + n * (L - P1).dot(n), B = P2 + n * (L - P2).dot(n);
        const QPointF sA = S(A), sB = S(B);
        extension(S(P1), sA);
        extension(S(P2), sB);
        p.drawLine(sA, sB);
        if(pxDist(sA, sB) > 20.0) {
            drawArrow(p, sA, sA - sB, col);
            drawArrow(p, sB, sB - sA, col);
        } else {
            drawArrow(p, sA, sB - sA, col);
            drawArrow(p, sB, sA - sB, col);
        }
        const double t = (L - A).dot(dir), len = (B - A).dot(dir);
        if(t < std::min(0.0, len) || t > std::max(0.0, len)) p.drawLine(std::fabs(t) < std::fabs(t - len) ? sA : sB, sL);
    };

    switch(c.type) {
    case SkCon::Distance: {
        Vec2 a, b;
        if(!(c.e2 == 0 && lineEnds(c.e1, a, b))) {
            a = posOf(c.e1);
            b = posOf(c.e2);
        }
        Vec2 dir = b - a;
        linear(a, b, dir.length() > 1e-12 ? dir.normalized() : Vec2(1, 0));
        break;
    }
    case SkCon::HDistance: linear(posOf(c.e1), posOf(c.e2), Vec2(1, 0)); break;
    case SkCon::VDistance: linear(posOf(c.e1), posOf(c.e2), Vec2(0, 1)); break;
    case SkCon::PointLineDistance: {
        int pt = c.e1, line = c.e2;
        Vec2 a, b;
        if(!lineEnds(line, a, b)) {
            std::swap(pt, line);
            if(!lineEnds(line, a, b)) break;
        }
        const Vec2 pp = posOf(pt), d = (b - a).normalized();
        const Vec2 foot = a + d * (pp - a).dot(d);
        Vec2 dir = foot - pp;
        linear(pp, foot, dir.length() > 1e-12 ? dir.normalized() : d.perp());
        break;
    }
    case SkCon::Radius:
    case SkCon::Diameter: {
        const SkEntity *e = s.find(c.e1);
        if(!e) break;
        const Vec2 C = s.pointPos(e->a);
        const double r = e->type == SkType::Circle ? e->r : s.arcRadius(*e);
        Vec2 u = L - C;
        u = u.length() > 1e-12 ? u.normalized() : Vec2(1, 0);
        const QPointF sC = S(C), sE = S(C + u * r);
        const bool outside = distance(L, C) > r;
        if(c.type == SkCon::Radius) {
            if(outside) {
                p.drawLine(sE, sL);
                drawArrow(p, sE, sE - sL, col);
            } else {
                p.drawLine(sC, sE);
                drawArrow(p, sE, sE - sC, col);
            }
        } else {
            const QPointF sE1 = S(C - u * r);
            p.drawLine(sE1, sE);
            drawArrow(p, sE1, sE1 - sC, col);
            drawArrow(p, sE, sE - sC, col);
            if(outside) p.drawLine(sE, sL);
        }
        break;
    }
    case SkCon::Angle: {
        Vec2 a1, b1, a2, b2;
        if(!lineEnds(c.e1, a1, b1) || !lineEnds(c.e2, a2, b2)) break;
        const Vec2 d1 = (b1 - a1).normalized(), d2 = (b2 - a2).normalized();
        if(std::fabs(d1.cross(d2)) < 1e-9) break;
        // The sector (between rays s1*d1 and s2*d2) that holds the label.
        const Vec2 l = (L - anchor).length() > 1e-12 ? (L - anchor).normalized() : (d1 + d2).normalized();
        double bestDot = -2;
        Vec2 r1 = d1, r2 = d2;
        for(int s1 : {1, -1})
            for(int s2 : {1, -1}) {
                if((s1 * s2 < 0) != c.supplementary) continue;
                const Vec2 bis = (d1 * s1 + d2 * s2).normalized();
                if(bis.dot(l) > bestDot) {
                    bestDot = bis.dot(l);
                    r1 = d1 * s1;
                    r2 = d2 * s2;
                }
            }
        double a0 = r1.angle(), sweep = cad::normAngle(r2.angle() - a0);
        if(sweep > cad::kPi) {
            a0 = r2.angle();
            sweep = 2 * cad::kPi - sweep;
        }
        const double R = std::max(distance(L, anchor), 1e-6);
        QPolygonF arc;
        const int n = 32;
        for(int i = 0; i <= n; ++i) {
            const double t = a0 + sweep * i / n;
            arc << S(anchor + Vec2(std::cos(t), std::sin(t)) * R);
        }
        p.drawPolyline(arc);
        if(arc.size() > 2) {
            drawArrow(p, arc.first(), arc.first() - arc[1], col);
            drawArrow(p, arc.last(), arc.last() - arc[arc.size() - 2], col);
        }
        break;
    }
    default:
        break;
    }

    // The value, on a light plate.
    QFont f = p.font();
    f.setPixelSize(12);
    p.setFont(f);
    const QString text = dimensionText(c);
    const QFontMetricsF fm(f);
    QRectF r(0, 0, fm.horizontalAdvance(text) + 8, fm.height() + 2);
    r.moveCenter(sL);
    p.setPen(isSel || isHot ? QPen(col, 1.0) : Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, isSel || isHot ? 240 : 205));
    p.drawRoundedRect(r, 3, 3);
    p.setPen(col);
    p.drawText(r, Qt::AlignCenter, text);
    m_dimensionRects[c.id] = r;
}

void SketchEditor::paintDimensionPreview(QPainter &p, const SkConstraint &c) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const auto saved = m_dimensionRects;
    paintDimension(p, c);
    m_dimensionRects = saved;
    p.restore();
}

std::vector<SketchEditor::Glyph> SketchEditor::glyphs() const {
    const cad::Sketch &s = sketch();
    std::vector<Glyph> out;
    const Vec2 diag = Vec2(1, 1).normalized();
    // A spot on a curve (or at a point) to hang a glyph on, and which side.
    auto spot = [&](int id, Vec2 &pos, Vec2 &side) {
        if(id < 0) return false; // origin and axes carry no glyphs
        const SkEntity *e = s.find(id);
        if(!e) return false;
        switch(e->type) {
        case SkType::Point:
            pos = {e->x, e->y};
            side = diag;
            return true;
        case SkType::Line: {
            const Vec2 a = s.pointPos(e->a), b = s.pointPos(e->b);
            pos = (a + b) * 0.5;
            side = (b - a).length() > 1e-12 ? (b - a).normalized().perp() : diag;
            return true;
        }
        case SkType::Circle:
            pos = s.pointPos(e->a) + diag * e->r;
            side = diag;
            return true;
        case SkType::Arc: {
            const Vec2 c = s.pointPos(e->a), st = s.pointPos(e->b), en = s.pointPos(e->c);
            const double mid = (st - c).angle() + arcSweep(c, st, en) * 0.5;
            side = Vec2(std::cos(mid), std::sin(mid));
            pos = c + side * s.arcRadius(*e);
            return true;
        }
        }
        return false;
    };
    auto isPt = [&](int id) {
        const SkEntity *e = s.find(id);
        return e && e->type == SkType::Point;
    };
    for(const auto &c : s.constraints) {
        if(cad::isDimension(c.type)) continue;
        Vec2 pos, side;
        switch(c.type) {
        case SkCon::Horizontal:
        case SkCon::Vertical:
            if(isPt(c.e1) && (isPt(c.e2) || c.e2 == cad::kSketchOrigin)) {
                out.push_back({c.id, (posOf(c.e1) + posOf(c.e2)) * 0.5, diag});
            } else if(spot(c.e1, pos, side)) {
                out.push_back({c.id, pos, side});
            }
            break;
        case SkCon::Parallel:
        case SkCon::Perpendicular:
        case SkCon::Tangent:
        case SkCon::Equal:
            for(int id : {c.e1, c.e2})
                if(spot(id, pos, side)) out.push_back({c.id, pos, side});
            break;
        case SkCon::Coincident:
        case SkCon::PointOnCurve:
        case SkCon::Midpoint:
        case SkCon::Fix:
            if(spot(isPt(c.e1) ? c.e1 : c.e2, pos, side)) out.push_back({c.id, pos, Vec2(1, -1).normalized()});
            break;
        case SkCon::Concentric:
            if(const SkEntity *e = s.find(c.e1)) out.push_back({c.id, s.pointPos(e->a), diag});
            break;
        case SkCon::Symmetric:
            out.push_back({c.id, (posOf(c.e1) + posOf(c.e2)) * 0.5, diag});
            break;
        default:
            break;
        }
    }
    return out;
}

void SketchEditor::paintGlyphs(QPainter &p) {
    const Projector P(m_viewport);
    const cad::Sketch &s = sketch();
    std::map<std::pair<int, int>, int> stack;
    for(const Glyph &g : glyphs()) {
        const SkConstraint *c = s.findConstraint(g.constraint);
        if(!c) continue;
        const QPointF base = P(toWorld(g.anchor));
        QPointF off = P(toWorld(g.anchor + g.side * sketchUnitsPerPixel(g.anchor) * 10.0)) - base;
        const double l = std::hypot(off.x(), off.y());
        off = l > 1e-6 ? off / l * 15.0 : QPointF(12, -12);
        QPointF pos = base + off;
        const int k = stack[{int(std::floor(pos.x() / 8)), int(std::floor(pos.y() / 8))}]++;
        pos += QPointF(k * 20.0, 0.0);
        const QRectF r(pos.x() - 9, pos.y() - 9, 18, 18);
        const bool isSel = selectedConstraints.count(c->id) > 0;
        const bool isHot = hover.kind == Kind::Constraint && hover.id == c->id;
        const bool bad = contains(m_solve.failed, c->id);
        const QColor accent = bad ? kFailedColor : isSel ? kSelectedColor.darker(120) : kIconAccent;
        paintGlyphChip(p, r, icon(glyphIcon(c->type), accent), isSel || isHot ? accent : QColor(160, 170, 184),
                       isHot);
        m_glyphRects.push_back({c->id, r});
    }
}

} // namespace cadly
