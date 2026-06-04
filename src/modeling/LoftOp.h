#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Wire.hxx>
#include <vector>
#include <string>

class LoftOp : public Operation {
public:
    LoftOp();
    ~LoftOp() override = default;

    // Parameters
    void addProfile(const TopoDS_Wire& wire);
    // Profile with holes: `outer` is the outer boundary, `holes` the inner
    // boundary wires (e.g. the inner circle of a concentric pair). The holes
    // are lofted into their own inner solids and cut from the outer loft, so a
    // ring-section profile produces a tube instead of a solid cylinder.
    void addProfile(const TopoDS_Wire& outer, const std::vector<TopoDS_Wire>& holes);
    void clearProfiles();
    void setSolid(bool solid);   // true = solid, false = shell
    void setRuled(bool ruled);   // true = ruled surface, false = smooth

    // Getters
    bool isSolid() const { return m_solid; }
    bool isRuled() const { return m_ruled; }
    int profileCount() const { return static_cast<int>(m_profiles.size()); }

    // Operation interface
    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Loft"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "loft"; }
    OperationDiff captureDiff() const override;

private:
    std::vector<TopoDS_Wire> m_profiles;
    // Parallel to m_profiles: the hole wires for each profile (empty if none).
    // m_holeProfiles[i] are the holes of m_profiles[i].
    std::vector<std::vector<TopoDS_Wire>> m_holeProfiles;
    bool m_solid = true;
    bool m_ruled = false;

    // For undo
    int m_createdBodyId = -1;
};
