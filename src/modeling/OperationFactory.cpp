#include "OperationFactory.h"
#include "../core/Operation.h"

#include "PatternOp.h"
#include "ExtrudeOp.h"
#include "PushPullOp.h"
#include "RevolveOp.h"

namespace OperationFactory {

std::unique_ptr<Operation> create(const std::string& typeId) {
    // Ops that can rehydrate to a fully editable state on reload. Each must
    // implement serializeParams / deserializeParams / rehydrateFromReload.
    //   - "pattern":  whole-body reference + scalar params.
    //   - "extrude":  profile re-derived from a persistent sketch id (Tier 2a);
    //                 declines rehydration for face-driven extrudes.
    //   - "pushpull": per-target profiles re-derived from (sketch id, region);
    //                 declines when any target is a bare body face.
    //   - "revolve":  profile re-derived from its sketch; axis is geometric
    //                 (origin+direction) and serialises directly.
    if (typeId == "pattern")  return std::make_unique<PatternOp>();
    if (typeId == "extrude")  return std::make_unique<ExtrudeOp>();
    if (typeId == "pushpull") return std::make_unique<PushPullOp>();
    if (typeId == "revolve")  return std::make_unique<RevolveOp>();

    return nullptr;
}

} // namespace OperationFactory
