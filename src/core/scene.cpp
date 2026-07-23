#include "ospr/scene.h"

#include <stdexcept>
#include <vector>

#include "ospr/math.h"

namespace ospr {
namespace {

// Regular tetrahedron on alternating corners of the cube [-1,1]^3, one colour
// per vertex so the rasterised result shows barycentric interpolation. Windings
// are counter-clockwise seen from outside.
ospray::cpp::Geometry build_tetrahedron_mesh()
{
    const std::vector<Vec3> positions{
        {1.0f, 1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f},
        {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},
    };
    const std::vector<Vec4> colors{
        {0.90f, 0.15f, 0.15f, 1.0f},
        {0.15f, 0.80f, 0.25f, 1.0f},
        {0.20f, 0.35f, 0.95f, 1.0f},
        {0.95f, 0.85f, 0.15f, 1.0f},
    };
    const std::vector<Vec3ui> indices{{0, 1, 2}, {0, 2, 3}, {0, 3, 1}, {1, 3, 2}};

    ospray::cpp::Geometry mesh("mesh");
    mesh.setParam("vertex.position", ospray::cpp::CopiedData(positions));
    mesh.setParam("vertex.color", ospray::cpp::CopiedData(colors));
    mesh.setParam("index", ospray::cpp::CopiedData(indices));
    mesh.commit();
    return mesh;
}

ospray::cpp::World build_tetrahedron_world()
{
    ospray::cpp::Material material("obj");
    material.setParam("kd", Vec3{1.0f, 1.0f, 1.0f});
    material.commit();

    ospray::cpp::GeometricModel model(build_tetrahedron_mesh());
    model.setParam("material", material);
    model.commit();

    ospray::cpp::Group group;
    group.setParam("geometry", ospray::cpp::CopiedData(model));
    group.commit();

    ospray::cpp::Instance instance(group);
    instance.commit();

    ospray::cpp::Light ambient("ambient");
    ambient.setParam("intensity", 0.4f);
    ambient.commit();

    ospray::cpp::Light key("distant");
    key.setParam("direction", Vec3{-0.4f, -0.7f, -0.6f});
    key.setParam("intensity", 2.2f);
    key.commit();

    const std::vector<ospray::cpp::Light> lights{ambient, key};

    ospray::cpp::World world;
    world.setParam("instance", ospray::cpp::CopiedData(instance));
    world.setParam("light", ospray::cpp::CopiedData(lights));
    world.commit();
    return world;
}

} // namespace

ospray::cpp::World build_world(const SceneSpec& spec)
{
    if (spec.type == "tetrahedron")
        return build_tetrahedron_world();
    throw std::runtime_error("unknown scene type: " + spec.type);
}

} // namespace ospr
