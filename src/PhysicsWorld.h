#pragma once

#include <PxPhysicsAPI.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

using namespace physx;

// Custom allocator for PhysX
class PhysicsAllocator : public PxAllocatorCallback {
public:
    void* allocate(size_t size, const char*, const char*, int) override {
        return malloc(size);
    }
    void deallocate(void* ptr) override {
        free(ptr);
    }
};

// Custom error callback for PhysX
class PhysicsErrorCallback : public PxErrorCallback {
public:
    void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) override;
};

// Rigid body data for rendering
struct RigidBodyInstance {
    PxRigidDynamic* actor;
    glm::mat4 transform;
    glm::vec3 halfExtents;  // For box shapes
    float radius;           // For sphere shapes
    bool isBox;
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    bool init();
    void shutdown();

    // Simulation
    void simulate(float deltaTime);

    // Character controller
    void createCharacterController(float height, float radius, const glm::vec3& position);
    void moveCharacter(const glm::vec3& displacement, float deltaTime);
    glm::vec3 getCharacterPosition() const;
    bool isCharacterOnGround() const;

    // Rigid bodies
    void addBox(const glm::vec3& position, const glm::vec3& halfExtents, float mass = 10.0f);
    void addSphere(const glm::vec3& position, float radius, float mass = 10.0f);

    // Get rigid body transforms for rendering
    const std::vector<RigidBodyInstance>& getRigidBodies() const { return rigidBodies; }
    void updateRigidBodyTransforms();

    // Ground
    void createFlatHeightfield(float width, float depth, float y = 0.0f);

private:
    PhysicsAllocator allocator;
    PhysicsErrorCallback errorCallback;

    PxFoundation* foundation = nullptr;
    PxPhysics* physics = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxScene* scene = nullptr;
    PxMaterial* defaultMaterial = nullptr;
    PxControllerManager* controllerManager = nullptr;
    PxController* characterController = nullptr;
    PxPvd* pvd = nullptr;

    std::vector<RigidBodyInstance> rigidBodies;

    // Helper to convert PhysX transform to glm matrix
    static glm::mat4 pxTransformToGlm(const PxTransform& t);
    static PxVec3 glmToPx(const glm::vec3& v);
    static glm::vec3 pxToGlm(const PxVec3& v);
};
