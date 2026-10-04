// Deleting a sketch goes through History (DeleteSketchOp) so it can be undone:
// undo restores the same Sketch under the same id with its name and visibility.

#include "core/Document.h"
#include "core/History.h"
#include "modeling/Sketch.h"
#include "modeling/DeleteSketchOp.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <memory>

using materializr::Sketch;

TEST(DeleteSketch, UndoRestoresSameIdNameVisibility) {
    Document doc;
    History hist;
    auto sk = std::make_shared<Sketch>();
    int a = sk->addPoint(glm::vec2(0, 0));
    int b = sk->addPoint(glm::vec2(10, 0));
    sk->addLine(a, b);
    int id = doc.addSketch(sk, "Mine");
    doc.setSketchVisible(id, false);

    auto op = std::make_unique<DeleteSketchOp>();
    op->setSketchId(id);
    ASSERT_TRUE(hist.pushOperation(std::move(op), doc));
    EXPECT_EQ(doc.getSketch(id), nullptr);

    ASSERT_TRUE(hist.undo(doc));
    EXPECT_EQ(doc.getSketch(id), sk);
    EXPECT_EQ(doc.getSketchName(id), "Mine");
    EXPECT_FALSE(doc.isSketchVisible(id));

    ASSERT_TRUE(hist.redo(doc));
    EXPECT_EQ(doc.getSketch(id), nullptr);
    ASSERT_TRUE(hist.undo(doc));
    EXPECT_EQ(doc.getSketch(id), sk);
}

TEST(DeleteSketch, MissingSketchRefused) {
    Document doc;
    DeleteSketchOp op;
    op.setSketchId(42);
    EXPECT_FALSE(op.execute(doc));
}
