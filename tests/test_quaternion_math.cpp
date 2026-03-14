#include <doctest/doctest.h>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

#include "scene/RotationUtils.h"
#include "vegetation/TreeGenerator.h"
#include "vegetation/BranchGenerator.h"

// ============================================================================
// glm::rotation() — shortest-arc quaternion between unit vectors
// Used in: RotationUtils, BranchGenerator, TreeGenerator, FootPlacementIKSolver
// ============================================================================

TEST_SUITE("glm::rotation — shortest-arc quaternion") {
    TEST_CASE("aligned vectors produce identity") {
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::quat q = glm::rotation(up, up);
        CHECK(std::abs(q.w) == doctest::Approx(1.0f).epsilon(0.001));
    }

    TEST_CASE("opposite vectors produce 180-degree rotation") {
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 down(0.0f, -1.0f, 0.0f);
        glm::quat q = glm::rotation(up, down);
        glm::vec3 result = q * up;
        CHECK(result.x == doctest::Approx(down.x).epsilon(0.01));
        CHECK(result.y == doctest::Approx(down.y).epsilon(0.01));
        CHECK(result.z == doctest::Approx(down.z).epsilon(0.01));
    }

    TEST_CASE("90-degree rotation between orthogonal axes") {
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 forward(0.0f, 0.0f, 1.0f);
        glm::quat q = glm::rotation(up, forward);
        glm::vec3 result = q * up;
        CHECK(result.x == doctest::Approx(forward.x).epsilon(0.01));
        CHECK(result.y == doctest::Approx(forward.y).epsilon(0.01));
        CHECK(result.z == doctest::Approx(forward.z).epsilon(0.01));
    }

    TEST_CASE("arbitrary direction rotation") {
        glm::vec3 from = glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f));
        glm::vec3 to = glm::normalize(glm::vec3(-1.0f, 0.5f, 2.0f));
        glm::quat q = glm::rotation(from, to);
        glm::vec3 result = q * from;
        CHECK(result.x == doctest::Approx(to.x).epsilon(0.01));
        CHECK(result.y == doctest::Approx(to.y).epsilon(0.01));
        CHECK(result.z == doctest::Approx(to.z).epsilon(0.01));
    }

    TEST_CASE("produces unit quaternion") {
        glm::vec3 dirs[] = {
            glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1),
            glm::vec3(-1, 0, 0), glm::normalize(glm::vec3(1, 1, 1))
        };
        for (const auto& from : dirs) {
            for (const auto& to : dirs) {
                glm::quat q = glm::rotation(from, to);
                float len = glm::length(q);
                CHECK(len == doctest::Approx(1.0f).epsilon(0.01));
            }
        }
    }
}

// ============================================================================
// glm::angle() / glm::axis() — quaternion decomposition
// Used in: MotionObservationComputer, ObservationExtractor, MotionScheduler,
//          FootPlacementIKSolver, BranchGenerator, TreeGenerator
// ============================================================================

TEST_SUITE("glm::angle and glm::axis") {
    TEST_CASE("identity quaternion has zero angle") {
        glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
        float angle = glm::angle(identity);
        CHECK(angle == doctest::Approx(0.0f).epsilon(0.001));
    }

    TEST_CASE("90-degree rotation around Y") {
        glm::quat q = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
        float angle = glm::angle(q);
        glm::vec3 axis = glm::axis(q);
        CHECK(angle == doctest::Approx(glm::half_pi<float>()).epsilon(0.001));
        CHECK(axis.y == doctest::Approx(1.0f).epsilon(0.01));
    }

    TEST_CASE("180-degree rotation") {
        glm::quat q = glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
        float angle = glm::angle(q);
        CHECK(angle == doctest::Approx(glm::pi<float>()).epsilon(0.001));
    }

    TEST_CASE("roundtrip: angleAxis -> angle/axis") {
        float inputAngle = glm::radians(37.0f);
        glm::vec3 inputAxis = glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f));
        glm::quat q = glm::angleAxis(inputAngle, inputAxis);

        float outAngle = glm::angle(q);
        glm::vec3 outAxis = glm::axis(q);

        CHECK(outAngle == doctest::Approx(inputAngle).epsilon(0.001));
        CHECK(outAxis.x == doctest::Approx(inputAxis.x).epsilon(0.01));
        CHECK(outAxis.y == doctest::Approx(inputAxis.y).epsilon(0.01));
        CHECK(outAxis.z == doctest::Approx(inputAxis.z).epsilon(0.01));
    }

    TEST_CASE("angular velocity from quaternion difference") {
        // Simulate 90-degree rotation over 1 second
        glm::quat prev(1.0f, 0.0f, 0.0f, 0.0f);
        glm::quat curr = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
        float dt = 1.0f;

        glm::quat deltaRot = curr * glm::inverse(prev);
        if (deltaRot.w < 0.0f) deltaRot = -deltaRot;

        float angle = glm::angle(deltaRot);
        glm::vec3 axis = glm::axis(deltaRot);
        glm::vec3 angVel = axis * (angle / dt);

        CHECK(angVel.y == doctest::Approx(glm::half_pi<float>()).epsilon(0.01));
        CHECK(std::abs(angVel.x) < 0.01f);
        CHECK(std::abs(angVel.z) < 0.01f);
    }
}

// ============================================================================
// Quaternion perturbation — replaces euler convert-modify-convert cycle
// Used in: BranchGenerator, TreeGenerator
// ============================================================================

TEST_SUITE("Quaternion perturbation") {
    TEST_CASE("small perturbation stays close to original") {
        glm::quat original = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        float smallAngle = glm::radians(1.0f);

        glm::quat perturbation = glm::angleAxis(smallAngle, glm::vec3(1.0f, 0.0f, 0.0f))
                               * glm::angleAxis(smallAngle, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat perturbed = original * perturbation;

        // The angle between original and perturbed should be small
        glm::quat diff = perturbed * glm::inverse(original);
        if (diff.w < 0.0f) diff = -diff;
        float angleDiff = glm::angle(diff);
        CHECK(angleDiff < glm::radians(3.0f));  // Within ~3 degrees
    }

    TEST_CASE("zero perturbation preserves orientation") {
        glm::quat original = glm::angleAxis(glm::radians(60.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat perturbation = glm::angleAxis(0.0f, glm::vec3(1.0f, 0.0f, 0.0f))
                               * glm::angleAxis(0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat perturbed = original * perturbation;

        CHECK(glm::dot(original, perturbed) == doctest::Approx(1.0f).epsilon(0.001));
    }

    TEST_CASE("perturbation produces unit quaternion") {
        glm::quat original = glm::normalize(glm::quat(0.5f, 0.5f, 0.5f, 0.5f));
        float angle = glm::radians(10.0f);
        glm::quat perturbation = glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f))
                               * glm::angleAxis(-angle, glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat perturbed = original * perturbation;
        float len = glm::length(perturbed);
        CHECK(len == doctest::Approx(1.0f).epsilon(0.001));
    }
}

// ============================================================================
// RotationUtils — glm::rotation based implementation
// ============================================================================

TEST_SUITE("RotationUtils with glm::rotation") {
    TEST_CASE("rotationFromDirection matches glm::rotation for unit vectors") {
        glm::vec3 defaultDir(0.0f, -1.0f, 0.0f);
        glm::vec3 target = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));

        glm::quat q = RotationUtils::rotationFromDirection(target, defaultDir);
        glm::vec3 result = q * defaultDir;

        CHECK(result.x == doctest::Approx(target.x).epsilon(0.01));
        CHECK(result.y == doctest::Approx(target.y).epsilon(0.01));
        CHECK(result.z == doctest::Approx(target.z).epsilon(0.01));
    }

    TEST_CASE("rotationFromDirection handles anti-parallel") {
        glm::vec3 defaultDir(0.0f, -1.0f, 0.0f);
        glm::vec3 target(0.0f, 1.0f, 0.0f);

        glm::quat q = RotationUtils::rotationFromDirection(target, defaultDir);
        glm::vec3 result = q * defaultDir;

        CHECK(result.x == doctest::Approx(target.x).epsilon(0.01));
        CHECK(result.y == doctest::Approx(target.y).epsilon(0.01));
        CHECK(result.z == doctest::Approx(target.z).epsilon(0.01));
    }
}

// ============================================================================
// Heading-frame quaternion rotation — replaces manual sin/cos matrix multiply
// Used in: ObservationExtractor, MotionObservationComputer
// ============================================================================

TEST_SUITE("Heading-frame quaternion rotation") {
    TEST_CASE("quaternion Y-rotation matches manual sin/cos rotation") {
        float headingAngle = glm::radians(45.0f);
        glm::vec3 worldVel(3.0f, 1.0f, 4.0f);

        // Manual sin/cos approach (old)
        float cosH = std::cos(-headingAngle);
        float sinH = std::sin(-headingAngle);
        glm::vec3 manualLocal;
        manualLocal.x = cosH * worldVel.x + sinH * worldVel.z;
        manualLocal.y = worldVel.y;
        manualLocal.z = -sinH * worldVel.x + cosH * worldVel.z;

        // Quaternion approach (new)
        glm::quat invHeading = glm::angleAxis(-headingAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 quatLocal = invHeading * worldVel;

        CHECK(quatLocal.x == doctest::Approx(manualLocal.x).epsilon(0.001));
        CHECK(quatLocal.y == doctest::Approx(manualLocal.y).epsilon(0.001));
        CHECK(quatLocal.z == doctest::Approx(manualLocal.z).epsilon(0.001));
    }

    TEST_CASE("zero heading produces identity transform") {
        glm::vec3 worldVel(3.0f, 1.0f, 4.0f);
        glm::quat invHeading = glm::angleAxis(0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 result = invHeading * worldVel;

        CHECK(result.x == doctest::Approx(worldVel.x).epsilon(0.001));
        CHECK(result.y == doctest::Approx(worldVel.y).epsilon(0.001));
        CHECK(result.z == doctest::Approx(worldVel.z).epsilon(0.001));
    }

    TEST_CASE("180-degree heading reverses XZ") {
        float heading = glm::pi<float>();
        glm::vec3 worldVel(1.0f, 2.0f, 3.0f);
        glm::quat invHeading = glm::angleAxis(-heading, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 result = invHeading * worldVel;

        CHECK(result.x == doctest::Approx(-worldVel.x).epsilon(0.001));
        CHECK(result.y == doctest::Approx(worldVel.y).epsilon(0.001));
        CHECK(result.z == doctest::Approx(-worldVel.z).epsilon(0.001));
    }

    TEST_CASE("preserves vector magnitude") {
        float heading = glm::radians(73.0f);
        glm::vec3 worldVel(3.0f, 1.0f, 4.0f);
        glm::quat invHeading = glm::angleAxis(-heading, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 result = invHeading * worldVel;

        CHECK(glm::length(result) == doctest::Approx(glm::length(worldVel)).epsilon(0.001));
    }
}

// ============================================================================
// Foot alignment with glm::rotation and angle clamping
// Used in: FootPlacementIKSolver
// ============================================================================

TEST_SUITE("Foot alignment rotation clamping") {
    TEST_CASE("small angle — no clamping needed") {
        glm::vec3 footUp(0.0f, 1.0f, 0.0f);
        // 5 degrees tilt
        glm::vec3 targetUp = glm::normalize(glm::vec3(0.087f, 0.996f, 0.0f));
        float maxAngle = glm::radians(30.0f);

        glm::quat fullAlign = glm::rotation(footUp, targetUp);
        float fullAngle = glm::angle(fullAlign);

        CHECK(fullAngle < maxAngle);

        // The rotation should align footUp to targetUp
        glm::vec3 result = fullAlign * footUp;
        CHECK(result.x == doctest::Approx(targetUp.x).epsilon(0.01));
        CHECK(result.y == doctest::Approx(targetUp.y).epsilon(0.01));
    }

    TEST_CASE("large angle — clamping applied") {
        glm::vec3 footUp(0.0f, 1.0f, 0.0f);
        // ~60 degrees tilt
        glm::vec3 targetUp = glm::normalize(glm::vec3(0.866f, 0.5f, 0.0f));
        float maxAngle = glm::radians(20.0f);

        glm::quat fullAlign = glm::rotation(footUp, targetUp);
        float fullAngle = glm::angle(fullAlign);
        CHECK(fullAngle > maxAngle);

        // Clamp
        glm::vec3 axis = glm::axis(fullAlign);
        glm::quat clamped = glm::angleAxis(maxAngle, axis);
        float clampedAngle = glm::angle(clamped);
        CHECK(clampedAngle == doctest::Approx(maxAngle).epsilon(0.001));

        // Result should be rotated but by the clamped angle, not full
        glm::vec3 result = clamped * footUp;
        float resultAngle = std::acos(glm::clamp(glm::dot(footUp, result), -1.0f, 1.0f));
        CHECK(resultAngle == doctest::Approx(maxAngle).epsilon(0.01));
    }
}

// ============================================================================
// BranchGenerator — verify tree generation still produces valid geometry
// ============================================================================

TEST_SUITE("BranchGenerator quaternion math") {
    TEST_CASE("basic branch generation produces valid sections") {
        BranchGenerator gen;
        BranchConfig config;
        config.seed = 42;
        config.length = 2.0f;
        config.radius = 0.15f;
        config.sectionCount = 4;
        config.segmentCount = 4;
        config.gnarliness = 0.1f;

        GeneratedBranch result = gen.generate(config);

        CHECK(result.branches.size() == 1);
        CHECK(result.branches[0].sections.size() == 5);  // sectionCount + 1

        // All section orientations should be unit quaternions
        for (const auto& section : result.branches[0].sections) {
            float len = glm::length(section.orientation);
            CHECK(len == doctest::Approx(1.0f).epsilon(0.01));
        }
    }

    TEST_CASE("branch with force direction bends toward force") {
        BranchGenerator gen;
        BranchConfig config;
        config.seed = 42;
        config.length = 4.0f;
        config.radius = 0.15f;
        config.sectionCount = 8;
        config.segmentCount = 4;
        config.gnarliness = 0.0f;  // No random perturbation
        config.forceDirection = glm::vec3(1.0f, 0.0f, 0.0f);  // Force to +X
        config.forceStrength = 0.5f;

        GeneratedBranch result = gen.generate(config);
        CHECK(!result.branches.empty());

        // Last section should have some +X displacement compared to first
        const auto& sections = result.branches[0].sections;
        glm::vec3 endPos = sections.back().origin;
        CHECK(endPos.x > 0.0f);  // Should bend toward +X
    }

    TEST_CASE("branch with child branches produces multiple branches") {
        BranchGenerator gen;
        BranchConfig config;
        config.seed = 123;
        config.length = 3.0f;
        config.radius = 0.2f;
        config.sectionCount = 6;
        config.segmentCount = 4;
        config.childCount = 3;
        config.childStart = 0.3f;

        GeneratedBranch result = gen.generate(config);
        CHECK(result.branches.size() == 4);  // 1 main + 3 children
    }
}

// ============================================================================
// TreeGenerator — verify tree generation with quaternion-based force
// ============================================================================

TEST_SUITE("TreeGenerator quaternion math") {
    TEST_CASE("evergreen tree generation produces valid data") {
        TreeOptions options;
        options.seed = 42;
        options.type = TreeType::Evergreen;
        options.branch.levels = 1;
        options.branch.length = {5.0f, 2.0f, 1.0f, 0.5f};
        options.branch.radius = {0.3f, 0.1f, 0.05f, 0.02f};
        options.branch.sections = {8, 4, 3, 2};
        options.branch.segments = {6, 4, 3, 3};
        options.branch.children = {5, 0, 0, 0};
        options.branch.angle = {0.0f, 45.0f, 45.0f, 45.0f};
        options.branch.gnarliness = {0.1f, 0.1f, 0.1f, 0.1f};
        options.branch.twist = {0.0f, 0.0f, 0.0f, 0.0f};
        options.branch.taper = {0.8f, 0.8f, 0.8f, 0.8f};
        options.branch.start = {0.0f, 0.3f, 0.3f, 0.3f};
        options.branch.forceDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        options.branch.forceStrength = 0.0f;

        TreeGenerator gen;
        TreeMeshData meshData = gen.generate(options);

        CHECK(meshData.branches.size() > 1);  // trunk + children

        // All orientations should be unit quaternions
        for (const auto& branch : meshData.branches) {
            for (const auto& section : branch.sections) {
                float len = glm::length(section.orientation);
                CHECK(len == doctest::Approx(1.0f).epsilon(0.01));
            }
        }
    }

    TEST_CASE("tree with gravity force produces downward-leaning branches") {
        TreeOptions options;
        options.seed = 42;
        options.type = TreeType::Evergreen;
        options.branch.levels = 0;  // Just trunk
        options.branch.length = {8.0f, 2.0f, 1.0f, 0.5f};
        options.branch.radius = {0.3f, 0.1f, 0.05f, 0.02f};
        options.branch.sections = {10, 4, 3, 2};
        options.branch.segments = {6, 4, 3, 3};
        options.branch.children = {0, 0, 0, 0};
        options.branch.angle = {0.0f, 45.0f, 45.0f, 45.0f};
        options.branch.gnarliness = {0.0f, 0.0f, 0.0f, 0.0f};
        options.branch.twist = {0.0f, 0.0f, 0.0f, 0.0f};
        options.branch.taper = {0.5f, 0.8f, 0.8f, 0.8f};
        options.branch.start = {0.0f, 0.3f, 0.3f, 0.3f};
        // Apply sideways force
        options.branch.forceDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        options.branch.forceStrength = 0.5f;

        TreeGenerator gen;
        TreeMeshData meshData = gen.generate(options);

        CHECK(!meshData.branches.empty());
        const auto& trunk = meshData.branches[0];
        // Last section should have drifted in +X due to force
        CHECK(trunk.sections.back().origin.x > 0.0f);
    }
}

// ============================================================================
// glm::eulerAngles — replaces manual matrix-based euler extraction
// Used in: ObservationExtractor, MotionObservationComputer, GuiSceneGraphTab
// ============================================================================

TEST_SUITE("glm::eulerAngles") {
    TEST_CASE("identity quaternion gives zero angles") {
        glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 euler = glm::eulerAngles(identity);
        CHECK(euler.x == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(euler.y == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(euler.z == doctest::Approx(0.0f).epsilon(0.001));
    }

    TEST_CASE("90-degree Y rotation") {
        glm::quat q = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 euler = glm::eulerAngles(q);
        // For a pure Y-axis rotation, only Y euler component should be significant
        CHECK(std::abs(euler.y) == doctest::Approx(glm::half_pi<float>()).epsilon(0.01));
    }

    TEST_CASE("roundtrip quaternion -> euler -> quaternion") {
        glm::quat original = glm::angleAxis(glm::radians(30.0f), glm::normalize(glm::vec3(1.0f, 0.5f, 0.2f)));
        glm::vec3 euler = glm::eulerAngles(original);
        glm::quat reconstructed = glm::quat(euler);

        // They should represent the same rotation
        glm::vec3 testVec(1.0f, 2.0f, 3.0f);
        glm::vec3 r1 = original * testVec;
        glm::vec3 r2 = reconstructed * testVec;
        CHECK(r1.x == doctest::Approx(r2.x).epsilon(0.01));
        CHECK(r1.y == doctest::Approx(r2.y).epsilon(0.01));
        CHECK(r1.z == doctest::Approx(r2.z).epsilon(0.01));
    }
}
