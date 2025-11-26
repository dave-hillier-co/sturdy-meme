#include "PhysicsWorld.h"
#include <cstdio>
#include <characterkinematic/PxController.h>
#include <characterkinematic/PxCapsuleController.h>
#include <characterkinematic/PxControllerManager.h>

void PhysicsErrorCallback::reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) {
    const char* errorCode = "Unknown";
    switch (code) {
        case PxErrorCode::eNO_ERROR: errorCode = "No Error"; break;
        case PxErrorCode::eDEBUG_INFO: errorCode = "Debug Info"; break;
        case PxErrorCode::eDEBUG_WARNING: errorCode = "Debug Warning"; break;
        case PxErrorCode::eINVALID_PARAMETER: errorCode = "Invalid Parameter"; break;
        case PxErrorCode::eINVALID_OPERATION: errorCode = "Invalid Operation"; break;
        case PxErrorCode::eOUT_OF_MEMORY: errorCode = "Out of Memory"; break;
        case PxErrorCode::eINTERNAL_ERROR: errorCode = "Internal Error"; break;
        case PxErrorCode::eABORT: errorCode = "Abort"; break;
        case PxErrorCode::ePERF_WARNING: errorCode = "Performance Warning"; break;
        default: break;
    }
    printf("PhysX Error [%s]: %s (%s:%d)\n", errorCode, message, file, line);
}

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld() {
    shutdown();
}

bool PhysicsWorld::init() {
    // Create foundation
    foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
    if (!foundation) {
        printf("Failed to create PhysX foundation\n");
        return false;
    }

    // Optional: Create PVD for debugging (disabled in release)
#ifdef _DEBUG
    pvd = PxCreatePvd(*foundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    pvd->connect(*transport, PxPvdInstrumentationFlag::eALL);
#endif

    // Create physics
    physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), true, pvd);
    if (!physics) {
        printf("Failed to create PhysX physics\n");
        return false;
    }

    // Initialize extensions (needed for character controller)
    if (!PxInitExtensions(*physics, pvd)) {
        printf("Failed to initialize PhysX extensions\n");
        return false;
    }

    // Create CPU dispatcher
    dispatcher = PxDefaultCpuDispatcherCreate(2);
    if (!dispatcher) {
        printf("Failed to create PhysX CPU dispatcher\n");
        return false;
    }

    // Create scene
    PxSceneDesc sceneDesc(physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = dispatcher;
    sceneDesc.filterShader = PxDefaultSimulationFilterShader;

    scene = physics->createScene(sceneDesc);
    if (!scene) {
        printf("Failed to create PhysX scene\n");
        return false;
    }

    // Create default material (friction, restitution)
    defaultMaterial = physics->createMaterial(0.5f, 0.5f, 0.3f);

    // Create controller manager
    controllerManager = PxCreateControllerManager(*scene);

    printf("PhysX initialized successfully\n");
    return true;
}

void PhysicsWorld::shutdown() {
    rigidBodies.clear();

    if (characterController) {
        characterController->release();
        characterController = nullptr;
    }

    if (controllerManager) {
        controllerManager->release();
        controllerManager = nullptr;
    }

    if (scene) {
        scene->release();
        scene = nullptr;
    }

    if (dispatcher) {
        dispatcher->release();
        dispatcher = nullptr;
    }

    PxCloseExtensions();

    if (physics) {
        physics->release();
        physics = nullptr;
    }

    if (pvd) {
        pvd->release();
        pvd = nullptr;
    }

    if (foundation) {
        foundation->release();
        foundation = nullptr;
    }
}

void PhysicsWorld::simulate(float deltaTime) {
    if (!scene) return;

    // Cap delta time to prevent physics explosion
    float dt = std::min(deltaTime, 1.0f / 30.0f);

    scene->simulate(dt);
    scene->fetchResults(true);

    // Update rigid body transforms for rendering
    updateRigidBodyTransforms();
}

void PhysicsWorld::createCharacterController(float height, float radius, const glm::vec3& position) {
    if (!controllerManager) return;

    PxCapsuleControllerDesc desc;
    desc.height = height;
    desc.radius = radius;
    desc.position = PxExtendedVec3(position.x, position.y + height * 0.5f + radius, position.z);
    desc.material = defaultMaterial;
    desc.slopeLimit = cosf(glm::radians(45.0f));
    desc.stepOffset = 0.3f;
    desc.contactOffset = 0.1f;
    desc.reportCallback = nullptr;

    characterController = controllerManager->createController(desc);
    if (!characterController) {
        printf("Failed to create character controller\n");
    }
}

void PhysicsWorld::moveCharacter(const glm::vec3& displacement, float deltaTime) {
    if (!characterController) return;

    PxControllerFilters filters;
    characterController->move(glmToPx(displacement), 0.001f, deltaTime, filters);
}

glm::vec3 PhysicsWorld::getCharacterPosition() const {
    if (!characterController) return glm::vec3(0.0f);

    PxExtendedVec3 pos = characterController->getPosition();
    // Return foot position (controller position is at center of capsule)
    const PxCapsuleController* capsule = static_cast<const PxCapsuleController*>(characterController);
    float halfHeight = capsule->getHeight() * 0.5f + capsule->getRadius();
    return glm::vec3(pos.x, pos.y - halfHeight, pos.z);
}

bool PhysicsWorld::isCharacterOnGround() const {
    if (!characterController) return false;

    PxControllerState state;
    characterController->getState(state);
    return (state.collisionFlags & PxControllerCollisionFlag::eCOLLISION_DOWN) != 0;
}

void PhysicsWorld::addBox(const glm::vec3& position, const glm::vec3& halfExtents, float mass) {
    if (!physics || !scene) return;

    PxShape* shape = physics->createShape(PxBoxGeometry(halfExtents.x, halfExtents.y, halfExtents.z), *defaultMaterial);

    PxTransform transform(glmToPx(position));
    PxRigidDynamic* body = physics->createRigidDynamic(transform);
    body->attachShape(*shape);
    PxRigidBodyExt::updateMassAndInertia(*body, mass / (8.0f * halfExtents.x * halfExtents.y * halfExtents.z));

    scene->addActor(*body);
    shape->release();

    RigidBodyInstance instance;
    instance.actor = body;
    instance.halfExtents = halfExtents;
    instance.radius = 0.0f;
    instance.isBox = true;
    instance.transform = pxTransformToGlm(transform);

    rigidBodies.push_back(instance);
}

void PhysicsWorld::addSphere(const glm::vec3& position, float radius, float mass) {
    if (!physics || !scene) return;

    PxShape* shape = physics->createShape(PxSphereGeometry(radius), *defaultMaterial);

    PxTransform transform(glmToPx(position));
    PxRigidDynamic* body = physics->createRigidDynamic(transform);
    body->attachShape(*shape);
    PxRigidBodyExt::updateMassAndInertia(*body, mass);

    scene->addActor(*body);
    shape->release();

    RigidBodyInstance instance;
    instance.actor = body;
    instance.halfExtents = glm::vec3(0.0f);
    instance.radius = radius;
    instance.isBox = false;
    instance.transform = pxTransformToGlm(transform);

    rigidBodies.push_back(instance);
}

void PhysicsWorld::updateRigidBodyTransforms() {
    for (auto& rb : rigidBodies) {
        if (rb.actor && rb.actor->isSleeping() == false) {
            rb.transform = pxTransformToGlm(rb.actor->getGlobalPose());
        }
    }
}

void PhysicsWorld::createFlatHeightfield(float width, float depth, float y) {
    if (!physics || !scene) return;

    // For a flat heightfield, we can simply use a box shape as a static actor
    // This is simpler and more efficient than an actual heightfield for a flat surface
    float thickness = 1.0f;
    PxShape* shape = physics->createShape(
        PxBoxGeometry(width * 0.5f, thickness * 0.5f, depth * 0.5f),
        *defaultMaterial
    );

    PxTransform transform(PxVec3(0.0f, y - thickness * 0.5f, 0.0f));
    PxRigidStatic* ground = physics->createRigidStatic(transform);
    ground->attachShape(*shape);

    scene->addActor(*ground);
    shape->release();

    printf("Created flat ground: %.1f x %.1f at y=%.1f\n", width, depth, y);
}

glm::mat4 PhysicsWorld::pxTransformToGlm(const PxTransform& t) {
    PxMat44 pxMat(t);
    glm::mat4 result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result[i][j] = pxMat[i][j];
        }
    }
    return result;
}

PxVec3 PhysicsWorld::glmToPx(const glm::vec3& v) {
    return PxVec3(v.x, v.y, v.z);
}

glm::vec3 PhysicsWorld::pxToGlm(const PxVec3& v) {
    return glm::vec3(v.x, v.y, v.z);
}
