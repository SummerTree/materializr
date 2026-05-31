#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <vector>
#include <string>

class PushPullOp : public Operation {
public:
    struct Target {
        TopoDS_Face profile;
        int sourceBodyId = -1; // -1 means create as a new body
    };

    PushPullOp();
    ~PushPullOp() override = default;

    void setTargets(std::vector<Target> targets);
    void setDistance(double d); // signed: positive = extrude, negative = cut

    double getDistance() const { return m_distance; }

    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Push/Pull"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "pushpull"; }
    OperationDiff captureDiff() const override;

private:
    std::vector<Target> m_targets;
    double m_distance = 1.0;

    // Undo state
    std::vector<std::pair<int, TopoDS_Shape>> m_previousBodies; // sourceBody mutations
    std::vector<int> m_createdBodyIds;                          // NewBody additions

    // Persisted across undo so the next execute (redo) reinserts free-floating
    // bodies under their previous ids, letting Document's tombstone restore
    // bring folder / colour / visibility / name back.
    std::vector<int> m_reuseBodyIds;
    size_t m_reuseIdx = 0;
};
