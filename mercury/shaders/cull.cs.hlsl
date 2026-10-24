// GPU culling for the model renderer (vk_scene_renderer.cc): one thread per
// draw slot tests the slot against a view and appends an indexed indirect
// draw to one of four buckets, which the view then draws with
// vkCmdDrawIndexedIndirectCount:
//   bucket 0  single-sided        1  double-sided
//   bucket 2  single-sided, mirrored (negative scale)   3  double-sided, mirrored
//
// Descriptor set 0:
//   binding 0  CullData[]   per slot: world box and geometry
//   binding 1  uint[]       commands, 5 uints each (VkDrawIndexedIndirectCommand)
//   binding 2  uint[]       counts, one per bucket

// Must match CullData in vk_scene_renderer.h.
struct CullData
{
    float4 center;  // world box center
    float4 extent;  // world box half size
    uint index_count;
    uint first_index;
    int vertex_offset;
    uint flags;     // kCull* bits, layer in bits 8-12
};

static const uint kCullAlive = 1;
static const uint kCullTransparent = 2;
static const uint kCullDoubleSided = 4;
static const uint kCullMirrored = 8;
static const uint kCullCastShadows = 16;
static const uint kCullDynamic = 32;

[[vk::binding(0, 0)]] StructuredBuffer<CullData> cull_data;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> commands;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> counts;

struct CullPush
{
    // World-space planes (xyz normal pointing inside, w distance); a plane of
    // zeros accepts everything.
    float4 planes[6];
    // x: slots, y: first command of this view, z: its first count,
    // w: commands per bucket
    uint4 view;
    // x, y: a slot draws when (flags & x) == y; z: layer mask
    uint4 filter;
};
[[vk::push_constant]] CullPush push;

[numthreads(64, 1, 1)]
void Main(uint3 id : SV_DispatchThreadID)
{
    uint slot = id.x;
    if (slot >= push.view.x) return;
    CullData data = cull_data[slot];
    if ((data.flags & push.filter.x) != push.filter.y) return;
    uint layer = (data.flags >> 8) & 31;
    if ((push.filter.z & (1u << layer)) == 0) return;

    // Box against each plane: outside when even its nearest corner is.
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        float4 plane = push.planes[i];
        float reach = dot(abs(plane.xyz), data.extent.xyz);
        if (dot(plane.xyz, data.center.xyz) + plane.w < -reach) return;
    }

    uint bucket = ((data.flags & kCullDoubleSided) != 0 ? 1u : 0u) +
                  ((data.flags & kCullMirrored) != 0 ? 2u : 0u);
    uint index;
    InterlockedAdd(counts[push.view.z + bucket], 1u, index);
    uint base = (push.view.y + bucket * push.view.w + index) * 5;
    commands[base + 0] = data.index_count;
    commands[base + 1] = 1;
    commands[base + 2] = data.first_index;
    commands[base + 3] = asuint(data.vertex_offset);
    commands[base + 4] = slot;  // firstInstance: the draw's record
}
