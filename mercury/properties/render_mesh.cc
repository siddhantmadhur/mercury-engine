#include "render_mesh.h"

namespace Mercury {

namespace {

const std::vector<glm::mat4>* Globals(const RenderMesh& mesh, const ModelPose* pose) {
  if (pose && pose->valid && mesh.model &&
      pose->globals.size() == mesh.model->Nodes().size()) {
    return &pose->globals;
  }
  return mesh.model ? &mesh.model->BindGlobals() : nullptr;
}

glm::mat4 BaseInverse(const RenderMesh& mesh, const std::vector<glm::mat4>& globals) {
  if (mesh.base_node >= 0 && mesh.base_node < static_cast<int>(globals.size())) {
    return glm::inverse(globals[mesh.base_node]);
  }
  // The whole model, in model space (Model::ComputeGlobals already leaves
  // out the root's transform when it belongs to the object).
  return glm::mat4(1.0f);
}

}  // namespace

glm::mat4 RenderMesh::DrawMatrix(const RenderDraw& draw, const ModelPose* pose) const {
  if (draw.node < 0 || !model) return draw.local;
  const auto* globals = Globals(*this, pose);
  if (!globals || draw.node >= static_cast<int>(globals->size())) return draw.local;
  return BaseInverse(*this, *globals) * (*globals)[draw.node] * draw.local;
}

void RenderMesh::SkinMatrices(const RenderDraw& draw, const ModelPose* pose,
                              std::vector<glm::mat4>& out) const {
  out.clear();
  if (draw.skin < 0 || draw.skin >= static_cast<int>(skins.size()) || !model) return;
  const auto* globals = Globals(*this, pose);
  if (!globals) return;
  const glm::mat4 base = BaseInverse(*this, *globals);
  const auto& bones = skins[draw.skin].bones;
  out.reserve(bones.size());
  for (const auto& bone : bones) {
    if (bone.node < 0 || bone.node >= static_cast<int>(globals->size())) {
      out.push_back(glm::mat4(1.0f));
      continue;
    }
    out.push_back(base * (*globals)[bone.node] * bone.offset);
  }
}

void RenderMesh::ComputeBounds() {
  bool first = true;
  std::vector<glm::mat4> skin;
  for (const auto& draw : draws) {
    if (!draw.geometry) continue;
    const glm::mat4 m = DrawMatrix(draw, nullptr);
    const glm::vec3 lo = draw.geometry->bounds_min;
    const glm::vec3 hi = draw.geometry->bounds_max;
    for (int corner = 0; corner < 8; corner++) {
      const glm::vec3 p((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                        (corner & 4) ? hi.z : lo.z);
      // Skinned vertices are in bind (model) space already.
      const glm::vec3 w = draw.skin >= 0 ? p : glm::vec3(m * glm::vec4(p, 1.0f));
      bounds_min = first ? w : glm::min(bounds_min, w);
      bounds_max = first ? w : glm::max(bounds_max, w);
      first = false;
    }
  }
  if (first) bounds_min = bounds_max = glm::vec3(0.0f);
}

}  // namespace Mercury
