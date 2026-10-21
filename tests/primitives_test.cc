#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "properties/primitives.h"
#include "properties/transform.h"

using namespace Mercury;

namespace {

// Every triangle should face the way its vertex normals point (counter-
// clockwise seen from outside), every normal should be unit length, and for
// closed shapes centered on the origin the normals should point outwards.
void ExpectWellFormed(const Mesh& mesh, bool closed) {
  const auto& vertices = mesh.vertices_;
  const auto& indices = mesh.indices_;
  ASSERT_FALSE(vertices.empty());
  ASSERT_EQ(indices.size() % 3, 0u);
  for (const auto& vertex : vertices) {
    EXPECT_NEAR(glm::length(vertex.normal), 1.0f, 1e-4f);
  }
  for (size_t i = 0; i < indices.size(); i += 3) {
    ASSERT_LT(indices[i + 2], vertices.size());
    const Vertex& a = vertices[indices[i]];
    const Vertex& b = vertices[indices[i + 1]];
    const Vertex& c = vertices[indices[i + 2]];
    const glm::vec3 face =
        glm::cross(b.position - a.position, c.position - a.position);
    EXPECT_GT(glm::dot(face, a.normal + b.normal + c.normal), 0.0f)
        << "triangle " << i / 3 << " is wound the wrong way";
    if (closed) {
      const glm::vec3 center = (a.position + b.position + c.position) / 3.0f;
      EXPECT_GT(glm::dot(face, center), 0.0f)
          << "triangle " << i / 3 << " faces inwards";
    }
  }
}

}  // namespace

TEST(Primitives, ShapesAreWoundOutwardsWithUnitNormals) {
  ExpectWellFormed(Primitives::Cube(), true);
  ExpectWellFormed(Primitives::Sphere(), true);
  ExpectWellFormed(Primitives::Capsule(), true);
  ExpectWellFormed(Primitives::Cylinder(), true);
  ExpectWellFormed(Primitives::Plane(), false);
  ExpectWellFormed(Primitives::Quad(), false);
}

TEST(Primitives, SizesMatchTheirDocumentation) {
  auto expect_bounds = [](Mesh mesh, glm::vec3 half) {
    const mAABB aabb = mesh.GetAABB();
    for (int i = 0; i < 3; i++) {
      EXPECT_NEAR(aabb.min[i], -half[i], 1e-5f);
      EXPECT_NEAR(aabb.max[i], half[i], 1e-5f);
    }
  };
  expect_bounds(Primitives::Cube(), glm::vec3(0.5f));
  expect_bounds(Primitives::Sphere(), glm::vec3(0.5f));
  expect_bounds(Primitives::Capsule(), glm::vec3(0.5f, 1.0f, 0.5f));
  expect_bounds(Primitives::Cylinder(), glm::vec3(0.5f, 1.0f, 0.5f));
  expect_bounds(Primitives::Plane(), glm::vec3(0.5f, 0.0f, 0.5f));
  expect_bounds(Primitives::Quad(), glm::vec3(0.5f, 0.5f, 0.0f));
}

TEST(Primitives, ColorGoesToVerticesAndOpacity) {
  const Mesh mesh = Primitives::Sphere(glm::vec4(1.0f, 0.5f, 0.25f, 0.75f));
  EXPECT_EQ(mesh.vertices_.front().color, glm::vec3(1.0f, 0.5f, 0.25f));
  EXPECT_FLOAT_EQ(mesh.opacity_, 0.75f);
}

TEST(Primitives, MissingModelFileFails) {
  std::string error;
  EXPECT_FALSE(Primitives::LoadModel("/no/such/model.obj", glm::vec4(1), &error));
  EXPECT_FALSE(error.empty());
}

TEST(Transform, EulerAnglesRoundTripThroughQuaternions) {
  const glm::vec3 cases[] = {{0, 0, 0},     {45, 0, 45},  {-50, -30, 0},
                             {10, 20, 30},  {0, 170, 0},  {-89, 0, 0}};
  for (const glm::vec3& degrees : cases) {
    const glm::quat q = Transform::EulerToQuat(degrees);
    const glm::quat back = Transform::EulerToQuat(Transform::QuatToEuler(q));
    // q and -q are the same rotation.
    EXPECT_NEAR(std::abs(glm::dot(q, back)), 1.0f, 1e-5f)
        << degrees.x << ", " << degrees.y << ", " << degrees.z;
  }
}

TEST(Transform, RotationOrderIsXThenYThenZ) {
  // Turning +90 around X takes +Y to +Z; then +90 around Y takes +Z to +X.
  const glm::quat q = Transform::EulerToQuat(glm::vec3(90, 90, 0));
  const glm::vec3 up = q * glm::vec3(0, 1, 0);
  EXPECT_NEAR(up.x, 1.0f, 1e-5f);
  EXPECT_NEAR(up.y, 0.0f, 1e-5f);
  EXPECT_NEAR(up.z, 0.0f, 1e-5f);
}

TEST(Transform, RotationMatrixFollowsChanges) {
  Transform transform;
  transform.SetRotate(glm::vec3(0, 90, 0));
  transform.SnapInterpolation();
  transform.ClearDirty();
  transform.SetRotate(glm::vec3(0, 0, 0));
  transform.SnapInterpolation();
  transform.SetRotate(glm::vec3(0, 90, 0));
  transform.SnapInterpolation();
  // Back where the renderer last saw it, but the matrix must still be for
  // 90 degrees around Y: -Z forward turns to -X.
  const glm::vec3 forward =
      glm::mat3(transform.GetRotationMatrix()) * glm::vec3(0, 0, -1);
  EXPECT_NEAR(forward.x, -1.0f, 1e-5f);
  EXPECT_NEAR(forward.z, 0.0f, 1e-5f);
}
