#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <gp_Pln.hxx>
#include <string>

class SplitBodyOp : public Operation {
public:
    SplitBodyOp();
    ~SplitBodyOp() override = default;

    // Parameters
    void setBody(int id);
    void setSplitPlane(const gp_Pln& plane);

    // Getters
    int getBodyId() const { return m_bodyId; }
    int getSecondBodyId() const { return m_secondBodyId; }

    // Operation interface
    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Split Body"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "split_body"; }
    OperationDiff captureDiff() const override;
    std::vector<int> plannedBodyIds() const override { return {m_bodyId}; }

private:
    int m_bodyId = -1;
    gp_Pln m_splitPlane;
    TopoDS_Shape m_previousShape;
    int m_secondBodyId = -1;
};
