#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "terracotta2/UI/Widget.hpp"

namespace Terracotta2 {

class DockSpace;

// Node of the binary split tree.
//  Leaf:  [TabBar][content of active tab]  (inactive tab contents stay alive, hidden)
//  Split: [first][Splitter][second]        (row for horizontal, column for vertical)
class DockNode : public Widget {
public:
    DockNode();
    ~DockNode() override;

    bool isLeaf() const { return m_first == nullptr; }
    bool isHorizontal() const { return m_horizontal; }
    DockNode* first() const { return m_first; }
    DockNode* second() const { return m_second; }
    DockSpace* dockSpace() const;

    // Leaf only. The leaf becomes a split; its existing tabs move into `first`.
    std::pair<DockNode*, DockNode*> splitHorizontal(float ratio); // first = left
    std::pair<DockNode*, DockNode*> splitVertical(float ratio);   // first = top
    float ratio() const { return m_ratio; }
    void setRatio(float ratio); // clamped to [0.05, 0.95]

    // ---- tabs (leaf only) ----
    // Sets the title of the active tab, creating an empty tab if there is none.
    void setTabTitle(std::string title);
    // Replaces the content of the active tab (creating an untitled tab if none).
    template <typename T, typename... Args>
    T& setContent(Args&&... args) {
        return static_cast<T&>(*setContent(std::make_unique<T>(std::forward<Args>(args)...)));
    }
    Widget* setContent(std::unique_ptr<Widget> content);
    template <typename T, typename... Args>
    T& addTab(std::string title, Args&&... args) {
        return static_cast<T&>(*addTab(std::move(title), std::make_unique<T>(std::forward<Args>(args)...)));
    }
    Widget* addTab(std::string title, std::unique_ptr<Widget> content); // becomes active
    int tabCount() const { return static_cast<int>(m_tabs.size()); }
    int activeTab() const { return m_active; }
    void setActiveTab(int index);
    const std::string& tabTitle(int index) const;
    // Removes a tab, returning its content (may be null). Does not collapse the node.
    std::unique_ptr<Widget> takeTab(int index, std::string* titleOut = nullptr);

private:
    friend class DockSpace;
    struct Tab {
        std::string title;
        Widget* content = nullptr; // child of m_contentArea
        Widget* button = nullptr;  // child of m_tabBar
    };
    friend class DockTabButton; // defined in DockSpace.cpp
    friend class DockSplitter;  // defined in DockSpace.cpp
    friend struct DockDrag;     // drag session, defined in DockSpace.cpp
    // existingToSecond: current tabs go to the second child (Left/Top drops) instead of the first.
    std::pair<DockNode*, DockNode*> split(bool horizontal, float ratio, bool existingToSecond = false);
    void ensureLeafChrome();
    void applyActive(); // visibility / flexGrow / button styling from m_active

    // leaf
    Widget* m_tabBar = nullptr;
    Widget* m_contentArea = nullptr;
    std::vector<Tab> m_tabs;
    int m_active = -1;
    // split
    DockNode* m_first = nullptr;
    DockNode* m_second = nullptr;
    Widget* m_splitter = nullptr;
    bool m_horizontal = true;
    float m_ratio = 0.5f;
};

// Workspace root of one Window. Docking works across windows, ImGui-style:
//  - drag a tab within/between windows: drop on a leaf's center (merge as tab) or edge band (split);
//  - drag a tab outside every window: it becomes a floating window that follows the cursor
//    (live), and can be dropped onto any dock space to re-dock;
//  - dragging the only tab of a floating window moves that window;
//  - closing a floating window re-docks its tabs into the main window; a floating
//    window whose last tab is dragged out closes itself.
class DockSpace : public Widget {
public:
    DockSpace();
    ~DockSpace() override;

    DockNode* rootNode() const;
    std::pair<DockNode*, DockNode*> splitHorizontal(float ratio) { return rootNode()->splitHorizontal(ratio); }
    std::pair<DockNode*, DockNode*> splitVertical(float ratio) { return rootNode()->splitVertical(ratio); }

    static DockSpace* of(Window* window); // the DockSpace in a window's tree (first found), or null

    // Opens a floating window (with its own DockSpace) holding one tab. Returns its leaf.
    template <typename T, typename... Args>
    T& floatTab(std::string title, SkRect screenFrame, Args&&... args) {
        DockNode* leaf = floatTab(std::move(title), std::make_unique<T>(std::forward<Args>(args)...), screenFrame);
        return static_cast<T&>(*leaf->m_tabs.back().content);
    }
    DockNode* floatTab(std::string title, std::unique_ptr<Widget> content, SkRect screenFrame);

    enum class DropZone { None, Center, Left, Right, Top, Bottom };

    // Starts a drag session (driven by Application's global drag). Called by tab widgets.
    // grabOffset: cursor position relative to the tab's top-left, kept while a floating window follows.
    void beginTabDrag(DockNode* source, int tabIndex, SkPoint grabOffset);
    void cancelTabDrag();
    static bool isDragging();

    // Moves a tab to a target leaf, possibly in another window (Center = merge, edge = split).
    // Collapses an emptied source leaf; closes an emptied floating window.
    void moveTab(DockNode* source, int tabIndex, DockNode* target, DropZone zone);

    void onDrawOver(SkCanvas* canvas) override; // drop preview

private:
    friend class DockNode;
    friend struct DockDrag;     // drag session, defined in DockSpace.cpp
    void tidy(DockNode* source); // after a tab left `source`: collapse it / close its emptied floating window
    void collapse(DockNode* emptyLeaf); // replace parent split by the sibling
    std::pair<DockNode*, DropZone> dropTarget(float x, float y) const; // window-local coords
    SkRect previewRect(DockNode* target, DropZone zone) const;
    void setDrop(DockNode* target, DropZone zone); // updates preview, damaging old + new rect only

    DockNode* m_dropNode = nullptr;
    DropZone m_dropZone = DropZone::None;
    SkRect m_preview = SkRect::MakeEmpty();
    // ponytail: drag-session state lives in DockSpace.cpp (one global drag at a time)
};

} // namespace Terracotta2
