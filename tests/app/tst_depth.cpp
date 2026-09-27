// Depth precision: highlights and edges must win cleanly against the faces
// they lie on, on every GPU. Apple GPUs (a float depth buffer) ignore the
// constant depth bias of the body pipeline, which showed as dark streaks
// through a highlighted cylinder; CADLY_NO_CONSTANT_DEPTH_BIAS reproduces that
// on any GPU.
#include "TestRegistry.h"

#include "selftest/TestUtil.h"
#include "viewport/Camera.h"
#include "viewport/Viewport.h"

#include "mesh/MeshData.h"
#include "topo/NamedShape.h"

#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <QtTest>

#include <cmath>

using namespace cadly;

namespace {

constexpr float kRadius = 25.0f, kHeight = 30.0f;

std::shared_ptr<const cad::MeshData> cylinderMesh() {
    const TopoDS_Shape s = BRepPrimAPI_MakeCylinder(kRadius, kHeight).Shape();
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(s, TopAbs_FACE, faces);
    std::vector<cad::FaceLabel> labels;
    for(int i = 1; i <= faces.Extent(); ++i) labels.push_back({"f" + std::to_string(i), {}});
    const cad::NamedShape named(s, labels);
    return cad::tessellateForDisplay(named, cad::defaultDeflection(2 * kRadius));
}

// The 1-based index of the face whose normals are horizontal (the side) or point up (the top).
int faceWhere(const cad::MeshData &m, bool side) {
    for(size_t f = 0; f < m.faceRanges.size(); ++f) {
        const auto &r = m.faceRanges[f];
        if(!r.count) continue;
        const float nz = m.normals[3 * m.indices[r.first] + 2];
        if(side ? std::fabs(nz) < 0.3f : nz > 0.9f) return int(f) + 1;
    }
    return 0;
}

} // namespace

class DepthTests : public QObject {
    Q_OBJECT

    std::unique_ptr<Viewport> m_vp;
    std::shared_ptr<const cad::MeshData> m_mesh;

    QColor pixel(const QImage &img, const QVector3D &p) const {
        const QPointF s = m_vp->camera().project(p);
        return img.pixelColor(int(s.x() * img.width() / m_vp->width()), int(s.y() * img.height() / m_vp->height()));
    }

    void show(DisplayStyle style, const QColor &highlight) {
        RenderScene scene;
        RenderBody body;
        body.mesh = m_mesh;
        body.color = QColor(176, 186, 198);
        scene.bodies.push_back(body);
        m_vp->setContent(scene, {PickTarget{cad::BodyId("Body1"), m_mesh}});
        m_vp->setDisplayStyle(style);
        m_vp->setHighlights({FaceHighlight{m_mesh, faceWhere(*m_mesh, true), highlight}}, {}, {}, {});
        m_vp->setStandardView(StandardView::Home, false);
        QVERIFY(waitForFrames(m_vp.get(), 3));
    }

private slots:
    void initTestCase() {
        qputenv("CADLY_NO_CONSTANT_DEPTH_BIAS", "1");
        m_mesh = cylinderMesh();
        QVERIFY(m_mesh && faceWhere(*m_mesh, true) && faceWhere(*m_mesh, false));
    }

    void init() {
        m_vp = std::make_unique<Viewport>();
        m_vp->resize(900, 700);
        m_vp->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_vp.get()));
    }

    void cleanup() { m_vp.reset(); }
    void cleanupTestCase() { qunsetenv("CADLY_NO_CONSTANT_DEPTH_BIAS"); }

    // A normal view keeps the near plane close to the model, not to the grid
    // around it: depth precision is what keeps overlays on top.
    void theNearPlaneFollowsTheModelNotTheGrid() {
        show(DisplayStyle::Shaded, QColor(30, 115, 230, 140));
        const Camera &c = m_vp->camera();
        QVERIFY2(c.nearPlane > 0.1f * c.distance,
                 qPrintable(QStringLiteral("near %1 at distance %2").arg(c.nearPlane).arg(c.distance)));
        QVERIFY(c.farPlane > c.distance * 2.0f);
    }

    // The highlighted side is one clean colour (no body streaking through),
    // and the plain top face is one colour too (no seam or line across it).
    void highlightsAndFacesHaveNoZFighting() {
        for(DisplayStyle style : {DisplayStyle::Shaded, DisplayStyle::ShadedWithEdges, DisplayStyle::Rendered}) {
            show(style, QColor(30, 115, 230, 255)); // opaque: any body pixel showing through stands out
            const QImage img = m_vp->grabFramebuffer();
            if(qEnvironmentVariableIsSet("CADLY_DEPTH_SHOTS"))
                img.save(qEnvironmentVariable("CADLY_DEPTH_SHOTS") + QStringLiteral("/depth_style%1.png").arg(int(style)));
            const QVector3D toEye = -m_vp->camera().forward();
            const float facing = std::atan2(toEye.y(), toEye.x());
            int bad = 0, total = 0;
            for(int a = -40; a <= 40; a += 2) {
                const float t = facing + float(a) * float(M_PI) / 180.0f;
                for(float z = 2.0f; z <= kHeight - 2.0f; z += 1.0f) {
                    const QColor c = pixel(img, QVector3D(kRadius * std::cos(t), kRadius * std::sin(t), z));
                    ++total;
                    if(!colorNear(c, QColor(30, 115, 230), 6)) ++bad;
                }
            }
            if(bad) qWarning("style %d: %d bad side pixels", int(style), bad);
            QVERIFY2(bad == 0, qPrintable(QStringLiteral("style %1: %2 of %3 side pixels are not the highlight")
                                              .arg(int(style)).arg(bad).arg(total)));
            // The top: flat, so lit the same everywhere (the rendered style's
            // specular aside, which changes smoothly).
            int lo = 255, hi = 0;
            for(float x = -18.0f; x <= 18.0f; x += 1.0f)
                for(float y = -18.0f; y <= 18.0f; y += 1.0f) {
                    if(x * x + y * y > 18.0f * 18.0f) continue;
                    const int v = qGray(pixel(img, QVector3D(x, y, kHeight)).rgb());
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            QVERIFY2(hi - lo <= (style == DisplayStyle::Rendered ? 30 : 6),
                     qPrintable(QStringLiteral("style %1: top face greys %2..%3").arg(int(style)).arg(lo).arg(hi)));
        }
    }
};

CADLY_REGISTER_TEST(DepthTests)

#include "tst_depth.moc"
