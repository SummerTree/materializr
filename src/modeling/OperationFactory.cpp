#include "OperationFactory.h"
#include "../core/Operation.h"

#include "PatternOp.h"
#include "ExtrudeOp.h"

namespace OperationFactory {

std::unique_ptr<Operation> create(const std::string& typeId) {
    // Ops that can rehydrate to a fully editable state on reload. Each must
    // implement serializeParams / deserializeParams / rehydrateFromReload.
    //   - "pattern": whole-body reference + scalar params.
    //   - "extrude": profile re-derived from a persistent sketch id (Tier 2a);
    //                declines rehydration for face-driven extrudes.
    if (typeId == "pattern") return std::make_unique<PatternOp>();
    if (typeId == "extrude") return std::make_unique<ExtrudeOp>();

    return nullptr;
}

} // namespace OperationFactory
