#include <doctest/doctest.h>
#include "scene/Transform.h"
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/quaternion.hpp>

TEST_CASE("Transform - default construction") {
    Transform t;
    CHECK(t.position == glm::vec3(0.0f));
    CHECK(t.rotation == glm::quat(1.0f, 0.0f, 0.0f, 0.0f));  // Identity
    CHECK(t.scale == glm::vec3(1.0f));
}

TEST_CASE("Transform - toMatrix produces correct TRS") {
    Transform t;
    t.position = glm::vec3(1.0f, 2.0f, 3.0f);
    t.scale = glm::vec3(2.0f);

    glm::mat4 m = t.toMatrix();

    // Check translation is in the last column
    CHECK(m[3][0] == doctest::Approx(1.0f));
    CHECK(m[3][1] == doctest::Approx(2.0f));
    CHECK(m[3][2] == doctest::Approx(3.0f));
}

TEST_CASE("Transform - direction vectors") {
    Transform t;
    // Identity rotation should give standard basis vectors
    CHECK(glm::length(t.forward() - glm::vec3(0, 0, 1)) < 0.001f);
    CHECK(glm::length(t.right() - glm::vec3(1, 0, 0)) < 0.001f);
    CHECK(glm::length(t.up() - glm::vec3(0, 1, 0)) < 0.001f);
}

TEST_CASE("TransformHandle - null handle") {
    TransformHandle h;
    CHECK(!h.isValid());
    CHECK(h == TransformHandle::null());
}

TEST_CASE("TransformHierarchy - create and destroy") {
    TransformHierarchy hierarchy;

    auto root = hierarchy.create("root");
    CHECK(hierarchy.isValid(root));
    CHECK(hierarchy.count() == 1);
    CHECK(hierarchy.getName(root) == "root");

    hierarchy.destroy(root);
    CHECK(!hierarchy.isValid(root));
    CHECK(hierarchy.count() == 0);
}

TEST_CASE("TransformHierarchy - parent-child relationship") {
    TransformHierarchy hierarchy;

    auto parent = hierarchy.create("parent");
    auto child = hierarchy.create("child", parent);

    CHECK(hierarchy.getParent(child) == parent);
    CHECK(hierarchy.getChildren(parent).size() == 1);
    CHECK(hierarchy.getChildren(parent)[0] == child);
}

TEST_CASE("TransformHierarchy - world matrix propagation") {
    TransformHierarchy hierarchy;

    auto parent = hierarchy.create("parent");
    auto child = hierarchy.create("child", parent);

    // Move parent to (10, 0, 0)
    Transform parentLocal;
    parentLocal.position = glm::vec3(10.0f, 0.0f, 0.0f);
    hierarchy.setLocal(parent, parentLocal);

    // Move child to (5, 0, 0) in local space
    Transform childLocal;
    childLocal.position = glm::vec3(5.0f, 0.0f, 0.0f);
    hierarchy.setLocal(child, childLocal);

    // Child's world position should be (15, 0, 0)
    glm::vec3 worldPos = hierarchy.getWorldPosition(child);
    CHECK(worldPos.x == doctest::Approx(15.0f));
    CHECK(worldPos.y == doctest::Approx(0.0f));
    CHECK(worldPos.z == doctest::Approx(0.0f));
}

TEST_CASE("TransformHierarchy - reparenting") {
    TransformHierarchy hierarchy;

    auto a = hierarchy.create("a");
    auto b = hierarchy.create("b");
    auto child = hierarchy.create("child", a);

    CHECK(hierarchy.getParent(child) == a);

    // Reparent child from a to b
    hierarchy.setParent(child, b);

    CHECK(hierarchy.getParent(child) == b);
    CHECK(hierarchy.getChildren(a).empty());
    CHECK(hierarchy.getChildren(b).size() == 1);
}

TEST_CASE("TransformHierarchy - find by name") {
    TransformHierarchy hierarchy;

    auto a = hierarchy.create("alpha");
    auto b = hierarchy.create("beta");

    CHECK(hierarchy.findByName("alpha") == a);
    CHECK(hierarchy.findByName("beta") == b);
    CHECK(!hierarchy.findByName("gamma").isValid());
}

TEST_CASE("TransformHierarchy - generation invalidates stale handles") {
    TransformHierarchy hierarchy;

    auto handle = hierarchy.create("test");
    uint32_t index = handle.index;

    hierarchy.destroy(handle);
    CHECK(!hierarchy.isValid(handle));

    // Create a new node which may reuse the same index
    auto newHandle = hierarchy.create("test2");
    if (newHandle.index == index) {
        // Handle reused the slot but generation is different
        CHECK(newHandle.generation != handle.generation);
        CHECK(hierarchy.isValid(newHandle));
        CHECK(!hierarchy.isValid(handle));  // Old handle still invalid
    }
}

TEST_CASE("TransformHierarchy - destroy parent orphans children") {
    TransformHierarchy hierarchy;

    auto parent = hierarchy.create("parent");
    auto child1 = hierarchy.create("child1", parent);
    auto child2 = hierarchy.create("child2", parent);

    hierarchy.destroy(parent);

    // Children should become roots
    CHECK(hierarchy.isValid(child1));
    CHECK(hierarchy.isValid(child2));
    CHECK(!hierarchy.getParent(child1).isValid());
    CHECK(!hierarchy.getParent(child2).isValid());

    // Both should be in roots now
    auto& roots = hierarchy.getRoots();
    CHECK(std::find(roots.begin(), roots.end(), child1) != roots.end());
    CHECK(std::find(roots.begin(), roots.end(), child2) != roots.end());
}
