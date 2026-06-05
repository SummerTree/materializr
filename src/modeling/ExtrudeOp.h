#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <string>

enum class ExtrudeMode { NewBody, Union, Subtract, Intersect };
enum class ExtrudeDirection { Normal, Symmetric, Custom };

class ExtrudeOp : public Operation {
public:
    ExtrudeOp();
    ~ExtrudeOp() override = default;

    // Parameters
    void setProfile(const TopoDS_Shape& wire); // closed wire/face to extrude
    void setDistance(double distance);
    void setDirection(ExtrudeDirection dir);
    void setMode(ExtrudeMode mode);
    void setTargetBody(int bodyId); // for boolean modes
    void setDraftAngle(double degrees);
    // Remember the source sketch so we can rebuild the profile if the sketch
    // is later edited (constraint value change → cascade). -1 means "not from
    // a sketch" (e.g., a face-driven extrude) — those won't cascade.
    void setSketchSource(int sketchId) { m_sketchId = sketchId; }

    // Getters
    double getDistance() const { return m_distance; }
    ExtrudeDirection getDirection() const { return m_direction; }
    ExtrudeMode getMode() const { return m_mode; }
    int getTargetBody() const { return m_targetBodyId; }
    double getDraftAngle() const { return m_draftAngle; }
    int getSketchId() const { return m_sketchId; }

    // Re-derive m_profile from the current state of the source sketch, then
    // return true so the caller can re-execute() against the new wires.
    // Returns false if there's no source sketch, the sketch is gone, or the
    // current sketch has no closed profile to build a face from.
    bool rebuildProfileFromSketch(Document& doc);

    // Operation interface
    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Extrude"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "extrude"; }
    OperationDiff captureDiff() const override;
    std::vector<int> plannedBodyIds() const override {
        if (m_mode != ExtrudeMode::NewBody && m_targetBodyId >= 0)
            return {m_targetBodyId};
        return {};
    }
    std::string serializeParams() const override;
    bool deserializeParams(const std::string& blob) override;
    bool rehydrateFromReload(const ReloadState& state, Document& doc) override;

private:
    TopoDS_Shape m_profile;
    double m_distance = 10.0;
    ExtrudeDirection m_direction = ExtrudeDirection::Normal;
    ExtrudeMode m_mode = ExtrudeMode::NewBody;
    int m_targetBodyId = -1;
    double m_draftAngle = 0.0;

    // For undo
    int m_createdBodyId = -1;
    TopoDS_Shape m_previousTargetShape; // for boolean undo
    // Optional: id of the sketch this extrude was built from. Used by the
    // cascade-on-sketch-edit path to re-derive m_profile and re-execute.
    int m_sketchId = -1;
};
