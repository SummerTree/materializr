#pragma once

#include <map>
#include <memory>

class History;
class Document;
class SelectionManager;

namespace materializr {
class Sketch;
}

namespace materializr {

class PropertiesPanel {
public:
    PropertiesPanel();

    void setHistory(History* history);
    void setDocument(Document* doc);
    void setSelectionManager(const SelectionManager* sel);

    // Set which history step is being edited (-1 for none)
    void setEditingStep(int step);
    int getEditingStep() const;

    // Render. Returns true if a parameter was changed (needs history replay).
    bool render();

private:
    // Constraint editor for whichever sketch is currently selected (or for
    // the parent sketch of a selected region). Walks the live sketch's
    // constraints, lets the user retune dimensional ones inline, runs the
    // solver, and pushes a SketchEditOp on commit so the change is
    // undoable and survives save/load. `modified` is set true if a value
    // was committed this frame so the host can dirty its mesh + history.
    void renderSketchConstraintsPanel(int sketchId, bool& modified);

    History* m_history = nullptr;
    Document* m_document = nullptr;
    const SelectionManager* m_selection = nullptr;
    int m_editingStep = -1;

    // Buffered text for each editable constraint value in the panel above.
    // Keyed by `constraint id`. Wiped when the panel switches to a
    // different sketch so values from the previous sketch don't leak in.
    int m_constraintPanelSketchId = -1;
    struct ConstraintEdit {
        char buf[24] = "0";
        bool focused = false;       // were we already focused-on last frame?
        // Snapshot of the sketch from the frame we first received focus,
        // used as the "before" state when we commit the SketchEditOp.
        std::shared_ptr<materializr::Sketch> beforeSnap;
    };
    std::map<int, ConstraintEdit> m_constraintEdits;
};

} // namespace materializr
