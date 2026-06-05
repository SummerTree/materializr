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

    // Cascade plumbing: remember which sketch + region(s) each target came
    // from when Push/Pull was triggered from sketch regions. The two arrays
    // are zip-aligned with m_targets: m_sketchSourceIds[i] is the sketch
    // that produced m_targets[i].profile, m_sketchSourceRegions[i] the
    // specific region index in that sketch (or -1 = "use first region").
    // -1 sketch id means a face-driven Push/Pull (no source sketch; that
    // target stays as-is during cascade).
    void setSketchSource(int targetIndex, int sketchId, int regionIndex = -1);
    bool hasAnySketchSource() const;
    int getSketchIdAt(int targetIndex) const;
    int targetCount() const { return static_cast<int>(m_targets.size()); }
    bool rebuildProfileFromSketch(Document& doc, int sketchId);

    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Push/Pull"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "pushpull"; }
    OperationDiff captureDiff() const override;
    std::vector<int> plannedBodyIds() const override {
        std::vector<int> ids;
        for (const auto& t : m_targets)
            if (t.sourceBodyId >= 0) ids.push_back(t.sourceBodyId);
        return ids;
    }
    std::string serializeParams() const override;
    bool deserializeParams(const std::string& blob) override;
    bool rehydrateFromReload(const ReloadState& state, Document& doc) override;

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

    // Cascade plumbing — see setSketchSource() in the public section.
    std::vector<int> m_sketchSourceIds;     // sketch id per target (-1 = none)
    std::vector<int> m_sketchSourceRegions; // region index per target (-1 = first)
    // Reload support for FACE-driven targets: per-target ordinal index of the
    // profile face within the source body's pre-op shape (see
    // SubShapeIndex.h). 0 = not a face target / not resolvable.
    std::vector<int> m_faceIndices;
};
