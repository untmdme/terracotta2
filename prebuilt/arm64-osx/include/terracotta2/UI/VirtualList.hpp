#pragma once
#include <functional>
#include <vector>
#include "terracotta2/UI/ScrollView.hpp"

namespace Terracotta2 {

// Long list of equal-height rows where only the rows in view exist: a small pool of row widgets
// is rebound as it scrolls, so a million items cost as much as a screenful.
//
//   list.add<VirtualList>(24)
//       .rows([](Widget& parent) -> Widget& { return parent.add<Label>(""); },
//             [&](Widget& row, int i) { static_cast<Label&>(row).text(items[i]); })
//       .count(int(items.size()));
//
// makeRow adds one empty row to `parent` and returns it; bindRow fills a row for item i (it is
// called again whenever a row is reused, so set everything that differs between items).
class VirtualList : public ScrollView {
public:
    using MakeRow = std::function<Widget&(Widget& parent)>;
    using BindRow = std::function<void(Widget& row, int index)>;
    explicit VirtualList(float rowHeight);

    VirtualList& rows(MakeRow make, BindRow bind);
    VirtualList& count(int n); // item count changed (rows in view are rebound)
    int count() const { return m_count; }
    void refresh();            // items changed: rebind and repaint the rows in view
    void scrollToRow(int index); // minimal scroll that shows it

    bool onScroll(float dx, float dy) override;
    bool onMouseMove(const MouseEvent& e) override; // scrollbar thumb drag
    void onLayout() override;

private:
    void sync(bool rebind = false);
    float m_rowH;
    int m_count = 0;
    Widget* m_content;
    std::vector<Widget*> m_pool;
    std::vector<int> m_bound; // item index each pool row shows, -1 = none
    MakeRow m_make;
    BindRow m_bind;
};

} // namespace Terracotta2
