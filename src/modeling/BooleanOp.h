#pragma once
#include "../core/Operation.h"
#include "../core/Document.h"
#include <TopoDS_Shape.hxx>
#include <string>

enum class BooleanMode { Union, Subtract, Intersect };

class BooleanOp : public Operation {
public:
    BooleanOp();
    ~BooleanOp() override = default;

    // Parameters
    void setTargetBodyId(int id);
    void setToolBodyId(int id);
    void setMode(BooleanMode mode);

    // Getters
    int getTargetBodyId() const { return m_targetBodyId; }
    int getToolBodyId() const { return m_toolBodyId; }
    BooleanMode getMode() const { return m_mode; }

    // Operation interface
    bool execute(Document& doc) override;
    bool undo(Document& doc) override;
    std::string name() const override { return "Boolean"; }
    std::string description() const override;
    void renderProperties() override;
    std::string typeId() const override { return "boolean"; }
    OperationDiff captureDiff() const override;

private:
    int m_targetBodyId = -1;
    int m_toolBodyId = -1;
    BooleanMode m_mode = BooleanMode::Union;

    // For undo
    TopoDS_Shape m_previousTargetShape;
    TopoDS_Shape m_previousToolShape;
    int m_removedToolId = -1;
};
