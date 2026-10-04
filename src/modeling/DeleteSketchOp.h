#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include "Sketch.h"
#include <memory>
#include <string>

// Delete a sketch from the project, undoably. The inverse of DuplicateSketchOp:
// execute() removes the sketch, undo() re-inserts the very same Sketch object
// under the SAME id (so bodies/ops that reference the id re-link), restoring
// its name and visibility. The shared_ptr keeps the geometry alive while the
// sketch is out of the document.
//
// Not carried across a save/reload: once the sketch is gone from the saved
// sketch list there is nothing to restore, so rehydrateFromReload() declines
// and the step reloads as an inert (baked) history entry.
class DeleteSketchOp : public Operation {
public:
    DeleteSketchOp() = default;
    ~DeleteSketchOp() override = default;

    void setSketchId(int id) { m_sketchId = id; }
    int sketchId() const { return m_sketchId; }

    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Delete Sketch"; }
    std::string description() const override;
    void renderProperties() override {}
    std::string typeId() const override { return "delete_sketch"; }

    std::string serializeParams() const override;
    bool deserializeParams(const std::string& blob) override;
    bool rehydrateFromReload(const ReloadState& state, Document& doc) override;

private:
    int m_sketchId = -1;
    std::shared_ptr<materializr::Sketch> m_sketch;  // held while deleted
    std::string m_name;
    bool m_visible = true;
};
