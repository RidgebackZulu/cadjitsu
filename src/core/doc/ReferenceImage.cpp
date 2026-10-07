#include "doc/ReferenceImage.h"

#include <cmath>

namespace cad {

namespace {

json vec(Vec2 v) { return json::array({v.x, v.y}); }

Vec2 vecFrom(const json &j, Vec2 fallback = {}) {
    if(!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) return fallback;
    return {j[0].get<double>(), j[1].get<double>()};
}

} // namespace

json ReferenceImage::toJson() const {
    json j{{"id", id},
           {"name", name},
           {"plane", plane.toJson()},
           {"image", imageKey},
           {"pixels", {pixelWidth, pixelHeight}},
           {"origin", vec(origin)},
           {"mmPerPixel", mmPerPixel},
           {"rotation", rotation},
           {"flip", flip},
           {"opacity", opacity},
           {"visible", visible}};
    if(perspective) {
        json corners = json::array();
        for(const Vec2 &c : perspective->corners) corners.push_back(vec(c));
        j["perspective"] = {{"corners", corners}, {"width", perspective->realWidth}, {"height", perspective->realHeight}};
    }
    if(lens) {
        json lines = json::array();
        for(const auto &l : lens->lines) {
            json pts = json::array();
            for(const Vec2 &p : l) pts.push_back(vec(p));
            lines.push_back(pts);
        }
        j["lens"] = {{"k1", lens->distortion.k1}, {"k2", lens->distortion.k2}, {"lines", lines}};
    }
    return j;
}

ReferenceImage ReferenceImage::fromJson(const json &j) {
    ReferenceImage r;
    if(!j.is_object()) return r;
    r.id = jget<int>(j, "id", 0);
    r.name = jget<std::string>(j, "name", "");
    r.plane = PlaneRef::fromJson(j.value("plane", json()));
    r.imageKey = jget<std::string>(j, "image", "");
    const auto px = jget<std::vector<int>>(j, "pixels", {});
    if(px.size() == 2) {
        r.pixelWidth = px[0];
        r.pixelHeight = px[1];
    }
    r.origin = vecFrom(j.value("origin", json()));
    r.mmPerPixel = jget<double>(j, "mmPerPixel", 0.1);
    if(!(r.mmPerPixel > 0)) r.mmPerPixel = 0.1;
    r.rotation = jget<double>(j, "rotation", 0.0);
    r.flip = jget<bool>(j, "flip", false);
    r.opacity = jget<double>(j, "opacity", 0.5);
    r.visible = jget<bool>(j, "visible", true);
    if(j.contains("perspective") && j["perspective"].is_object()) {
        const json &p = j["perspective"];
        Perspective ps;
        const json corners = p.value("corners", json::array());
        if(corners.is_array() && corners.size() == 4) {
            for(size_t k = 0; k < 4; ++k) ps.corners[k] = vecFrom(corners[k]);
            ps.realWidth = jget<double>(p, "width", 0.0);
            ps.realHeight = jget<double>(p, "height", 0.0);
            if(ps.realWidth > 0 && ps.realHeight > 0) r.perspective = ps;
        }
    }
    if(j.contains("lens") && j["lens"].is_object()) {
        const json &l = j["lens"];
        Lens lens;
        lens.distortion.k1 = jget<double>(l, "k1", 0.0);
        lens.distortion.k2 = jget<double>(l, "k2", 0.0);
        const json lines = l.value("lines", json::array());
        if(lines.is_array())
            for(const json &line : lines) {
                if(!line.is_array()) continue;
                std::vector<Vec2> pts;
                for(const json &p : line) pts.push_back(vecFrom(p));
                lens.lines.push_back(std::move(pts));
            }
        r.lens = std::move(lens);
    }
    return r;
}

Vec2 ReferenceImage::toPlane(Vec2 pixel) const {
    Vec2 d((pixel.x - pixelWidth / 2.0) * mmPerPixel, -(pixel.y - pixelHeight / 2.0) * mmPerPixel);
    if(flip) d.x = -d.x;
    const double a = rotation * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    return origin + Vec2(d.x * c - d.y * s, d.x * s + d.y * c);
}

Vec2 ReferenceImage::toPixel(Vec2 plane) const {
    const Vec2 r = plane - origin;
    const double a = -rotation * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    Vec2 d(r.x * c - r.y * s, r.x * s + r.y * c);
    if(flip) d.x = -d.x;
    return {d.x / mmPerPixel + pixelWidth / 2.0, -d.y / mmPerPixel + pixelHeight / 2.0};
}

std::array<Vec2, 4> ReferenceImage::planeCorners() const {
    const double w = pixelWidth, h = pixelHeight;
    return {toPlane({0, 0}), toPlane({w, 0}), toPlane({w, h}), toPlane({0, h})};
}

bool calibrateReferenceImage(ReferenceImage &image, Vec2 p1, Vec2 p2, double realDistance, std::string &error) {
    const double measured = distance(p1, p2);
    if(measured < 1e-9) {
        error = "pick two different points on the picture";
        return false;
    }
    if(!(realDistance > 0)) {
        error = "the real distance must be more than 0";
        return false;
    }
    const double k = realDistance / measured;
    image.mmPerPixel *= k;
    image.origin = p1 + (image.origin - p1) * k;
    return true;
}

} // namespace cad
