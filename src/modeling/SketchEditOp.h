#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include "Sketch.h"
#include <memory>

namespace materializr {

// Snapshot-based undo for sketch mutations (drawing elements, dimension input).
// Holds a shared_ptr to the live Sketch (same instance the Document and the
// active editing session share) plus before/after snapshots. Undo/redo swap
// the live sketch's contents in place, so the change is visible regardless of
// whether the sketch is in the document yet (a fresh sketch isn't added until
// exitSketchMode).
class SketchEditOp : public Operation {
public:
    SketchEditOp(std::shared_ptr<Sketch> liveSketch,
                 std::shared_ptr<Sketch> beforeSnapshot,
                 std::shared_ptr<Sketch> afterSnapshot);

    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Sketch Edit"; }
    std::string description() const override;
    // Lists every dimension constraint (Distance / Radius / Angle) on the
    // post-edit snapshot with an editable value. Mutating a value here updates
    // m_after AND re-solves the snapshot so dependent geometry shifts to match
    // before the user clicks Apply Changes. Non-dimensional constraints
    // (Horizontal, Parallel, etc.) are shown as read-only entries because
    // they have nothing user-tunable.
    void renderProperties() override;
    std::string typeId() const override { return "sketchedit"; }

    // Cross-session serialization: writes both snapshots in the project's
    // SKETCH_START/SKETCH_END text format so a reloaded op can rehydrate
    // into a real SketchEditOp (not a parameterless ReplayOp) and the
    // History → Properties editor still works. The Document& lets us look
    // up the live sketch's id from the held shared_ptr at write time;
    // deserialize is a free function in ProjectIO since it needs to
    // bind m_target to the loaded sketch by id.
    std::string serializeWithDocument(const Document& doc) const;

    // Accessors for the ProjectIO factory.
    std::shared_ptr<Sketch> getBeforeSnapshot() const { return m_before; }
    std::shared_ptr<Sketch> getAfterSnapshot() const  { return m_after;  }
    void setTarget(std::shared_ptr<Sketch> t) { m_target = std::move(t); }
    void setSnapshots(std::shared_ptr<Sketch> before, std::shared_ptr<Sketch> after) {
        m_before = std::move(before); m_after = std::move(after);
    }

private:
    std::shared_ptr<Sketch> m_target;
    std::shared_ptr<Sketch> m_before;
    std::shared_ptr<Sketch> m_after;
};

} // namespace materializr
