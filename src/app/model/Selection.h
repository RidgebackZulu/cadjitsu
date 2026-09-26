#pragma once

#include "viewport/Picker.h"

#include "base/Ids.h"

#include <string>
#include <vector>

namespace cadly {

// Something the user has selected in the canvas or browser.
struct SelectionItem {
    enum class Kind { Body, Face, Edge, Vertex, SketchEntity, Profile, Plane };
    Kind kind = Kind::Body;
    cad::BodyId body;               // Body / Face / Edge / Vertex
    int index = 0;                  // 1-based face / edge / vertex index
    cad::FeatureId feature = cad::kNoFeature; // SketchEntity / Profile / Plane
    int entity = 0;                 // sketch entity id
    std::string key;                // profile key

    bool operator==(const SelectionItem &) const = default;

    static SelectionItem fromPick(const PickHit &hit);
};

class SelectionSet {
public:
    const std::vector<SelectionItem> &items() const { return m_items; }
    bool empty() const { return m_items.empty(); }
    size_t size() const { return m_items.size(); }
    bool contains(const SelectionItem &it) const;
    void add(const SelectionItem &it);
    void remove(const SelectionItem &it);
    void toggle(const SelectionItem &it);
    void clear() { m_items.clear(); }
    std::vector<SelectionItem> ofKind(SelectionItem::Kind k) const;
    size_t count(SelectionItem::Kind k) const { return ofKind(k).size(); }
    bool operator==(const SelectionSet &o) const { return m_items == o.m_items; }

private:
    std::vector<SelectionItem> m_items;
};

} // namespace cadly
