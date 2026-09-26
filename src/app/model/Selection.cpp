#include "model/Selection.h"

#include <algorithm>

namespace cadly {

SelectionItem SelectionItem::fromPick(const PickHit &hit) {
    SelectionItem it;
    it.body = hit.body;
    it.index = hit.index;
    switch(hit.kind) {
    case PickHit::Kind::Face: it.kind = Kind::Face; break;
    case PickHit::Kind::Edge: it.kind = Kind::Edge; break;
    case PickHit::Kind::Vertex: it.kind = Kind::Vertex; break;
    case PickHit::Kind::None: it.kind = Kind::Body; break;
    }
    return it;
}

bool SelectionSet::contains(const SelectionItem &it) const {
    return std::find(m_items.begin(), m_items.end(), it) != m_items.end();
}

void SelectionSet::add(const SelectionItem &it) {
    if(!contains(it)) m_items.push_back(it);
}

void SelectionSet::remove(const SelectionItem &it) { m_items.erase(std::remove(m_items.begin(), m_items.end(), it), m_items.end()); }

void SelectionSet::toggle(const SelectionItem &it) {
    if(contains(it)) remove(it);
    else add(it);
}

std::vector<SelectionItem> SelectionSet::ofKind(SelectionItem::Kind k) const {
    std::vector<SelectionItem> out;
    for(const auto &it : m_items)
        if(it.kind == k) out.push_back(it);
    return out;
}

} // namespace cadly
