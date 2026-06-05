#pragma once
#include <vector>
#include <memory>
#include "Operation.h"
#include "Document.h"

namespace materializr { class EventBus; }

class History {
public:
    History();
    ~History() = default;

    void setEventBus(materializr::EventBus* bus) { m_eventBus = bus; }

    // Add a new operation (executes it and pushes to stack)
    bool pushOperation(std::unique_ptr<Operation> op, Document& doc);

    // Add an operation whose effect is already applied to the document.
    // Used for ops where the live mutation happened externally (e.g. sketch
    // edits performed by the SketchTool); the op snapshots before/after so
    // undo/redo can swap between them without re-running the original action.
    void pushExecuted(std::unique_ptr<Operation> op);

    // Undo/Redo
    bool canUndo() const;
    bool canRedo() const;
    bool undo(Document& doc);
    bool redo(Document& doc);

    // History navigation
    int stepCount() const;
    int currentStep() const; // index of last executed step
    const Operation* getStep(int index) const;

    // Thread-last reflow support: if `op` plans to touch a body that the
    // trailing Thread steps modified, returns the index those threads start
    // at (where the op should be inserted); -1 = no reflow needed.
    int reflowInsertionIndex(const Operation& op) const;
    // Insert `op` at `index`, executing it against the state rolled back to
    // just before `index`, then replay the displaced steps (the threads
    // re-cut parametrically on the new geometry). Returns false only if the
    // INSERTED op itself fails (history left as it was); a displaced step
    // failing to re-execute suspends with lastReplayFailure(), same as
    // editStep.
    bool insertStepAndReplay(int index, std::unique_ptr<Operation> op,
                             Document& doc);

    // Edit a historical step's parameters and replay. Editing a step ABOVE
    // the current index (e.g. one suspended by a failed recompute) rolls
    // forward to it instead of refusing; a successful edit also auto-retries
    // a failure-suspended tail.
    bool editStep(int index, Document& doc);

    // Index of the step that most recently failed to recompute during an
    // editStep replay / redo (its result vanished from the viewport and it
    // sits above the current index). -1 = none. The UI uses this to explain
    // what happened instead of leaving steps silently missing.
    int lastReplayFailure() const { return m_failedReplayAt; }

    // Remove a step entirely (delete that operation), rebuilding the model in
    // place. Returns false and leaves the model unchanged if removing the step
    // makes a later, dependent operation fail (a conflict).
    bool removeStep(int index, Document& doc);

    // Breakpoint: suppress all steps after this index
    void setBreakpoint(int index); // -1 = no breakpoint
    int getBreakpoint() const;

    // Replay: re-execute all enabled steps from scratch
    bool replayAll(Document& doc);

    // Clear history
    void clear();

    // Access operations for UI
    const std::vector<std::unique_ptr<Operation>>& operations() const;

private:
    std::vector<std::unique_ptr<Operation>> m_operations;
    int m_currentIndex = -1;
    int m_breakpoint = -1;
    // Step that failed to recompute during the last editStep/redo replay;
    // cleared by manual undo, by a successful retry, or by clear().
    int m_failedReplayAt = -1;
    materializr::EventBus* m_eventBus = nullptr;
};
