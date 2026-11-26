#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <vector>

namespace chroma
{
// §5 Undo/Redo and A/B compare on whole-state snapshots (message thread only).
// capture() returns the editable state (no global settings), restore() applies one.
class History
{
public:
    std::function<juce::ValueTree()> capture;
    std::function<void (const juce::ValueTree&)> restore;
    static constexpr int maxDepth = 50;

    // Records the current state as a new step if it differs from the last one. Returns true if recorded.
    bool commit()
    {
        auto s = capture();
        if (current.isValid() && s.isEquivalentTo (current)) return false;
        if (current.isValid())
        {
            undoStack.push_back (current);
            if ((int) undoStack.size() > maxDepth) undoStack.erase (undoStack.begin());
        }
        current = s;
        redoStack.clear();
        return true;
    }
    // Starts a fresh history from the current state (e.g. after the host restores a project).
    void reset() { undoStack.clear(); redoStack.clear(); current = capture(); }

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    bool undo()
    {
        commit();   // pending edits become a step first
        if (undoStack.empty()) return false;
        redoStack.push_back (current);
        current = undoStack.back();
        undoStack.pop_back();
        restore (current);
        return true;
    }
    bool redo()
    {
        if (commit()) return false;   // a new edit invalidated the redo stack
        if (redoStack.empty()) return false;
        undoStack.push_back (current);
        current = redoStack.back();
        redoStack.pop_back();
        restore (current);
        return true;
    }

    // A/B compare: two independent states; switching stores the live state in the active slot.
    int activeSlot() const noexcept { return ab; }
    void selectSlot (int which)
    {
        which = which != 0 ? 1 : 0;
        if (which == ab) return;
        abSlots[(size_t) ab] = capture();
        ab = which;
        if (abSlots[(size_t) ab].isValid()) restore (abSlots[(size_t) ab]);
        commit();
    }
    void copyActiveToOther() { abSlots[(size_t) (1 - ab)] = capture(); }

private:
    juce::ValueTree current;
    std::vector<juce::ValueTree> undoStack, redoStack;
    std::array<juce::ValueTree, 2> abSlots;
    int ab = 0;
};
} // namespace chroma
