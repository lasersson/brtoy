struct MeshInfo
{
    uint index_data_ptr;
    uint pos_data_ptr;
    uint pos_data_stride;
    uint attrib_data_ptr;
    uint attrib_data_stride;
    uint index_count;
};

struct InstanceInfo
{
    float4x4 transform;
    uint mesh_info_ptr;
};

struct DrawParams
{
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

struct WorldConstants
{
    float4x4 view_projection;
};

ByteAddressBuffer g_mesh_data : register(t0, space0);

StructuredBuffer<InstanceInfo> g_instances : register(t0, space1);
RWByteAddressBuffer g_visible_instances_rw : register(u1, space1);
ByteAddressBuffer g_visible_instances : register(t1, space1);

cbuffer WorldConstants : register(b2, space1)
{
    WorldConstants g_constants;
}

RWStructuredBuffer<DrawParams> g_draw_params : register(u0, space2);

MeshInfo loadMeshInfo(uint offset)
{
    MeshInfo mesh;
    mesh.index_data_ptr = g_mesh_data.Load(offset);
    mesh.pos_data_ptr = g_mesh_data.Load(offset += 4);
    mesh.pos_data_stride = g_mesh_data.Load(offset += 4);
    mesh.attrib_data_ptr = g_mesh_data.Load(offset += 4);
    mesh.attrib_data_stride = g_mesh_data.Load(offset += 4);
    mesh.index_count = g_mesh_data.Load(offset += 4);
    return mesh;
}

[numthreads(256, 1, 1)]
void cullInstances(uint3 thread_id : SV_DispatchThreadID)
{
    g_draw_params[0].vertex_count = 45;
    bool visible = false;

    uint instance_count;
    g_instances.GetDimensions(instance_count);
    uint instance_index = thread_id.x;
    if (instance_index < instance_count)
    {
        InstanceInfo instance = g_instances[instance_index];
        MeshInfo mesh = loadMeshInfo(instance.mesh_info_ptr);

        for (uint i = 0; i < mesh.index_count; ++i) {
            uint index = g_mesh_data.Load(mesh.index_data_ptr + 4 * i);
            float3 v_pos = asfloat(g_mesh_data.Load3(mesh.pos_data_ptr + mesh.pos_data_stride * index));
            float3 world_pos = mul(float4(v_pos, 1), instance.transform).xyz;
            float4 clip_pos = mul(float4(world_pos, 1), g_constants.view_projection);
            if (clip_pos.x > -clip_pos.w && clip_pos.x < clip_pos.w &&
                clip_pos.x > -clip_pos.w && clip_pos.x < clip_pos.w &&
                clip_pos.x > -clip_pos.w && clip_pos.x < clip_pos.w &&
                clip_pos.x > -clip_pos.w && clip_pos.x < clip_pos.w) {
                visible = true;
                break;
            } 
        }
    }

    if (visible) {
        uint out_instance_index;
        InterlockedAdd(g_draw_params[0].instance_count, 1, out_instance_index);
        g_visible_instances_rw.Store(out_instance_index * 4, instance_index);
    }
}

struct ClipVertex
{
    float4 pos : SV_Position;
    float3 normal : Normal;
};

ClipVertex vsMain(uint instance_id : SV_InstanceID, uint vertex_id : SV_VertexID)
{
    uint instance_index = g_visible_instances.Load(instance_id * 4);
    InstanceInfo instance = g_instances[instance_index];
    MeshInfo mesh = loadMeshInfo(instance.mesh_info_ptr);

    ClipVertex out_vertex;
    out_vertex.pos = float4(0,0,0,1);
    out_vertex.normal = float3(0,0,0);
    if (vertex_id < mesh.index_count)
    {
        uint index = g_mesh_data.Load(mesh.index_data_ptr + 4 * vertex_id);
        float3 v_pos = asfloat(g_mesh_data.Load3(mesh.pos_data_ptr + mesh.pos_data_stride * index));
        float3 v_normal = asfloat(g_mesh_data.Load3(mesh.attrib_data_ptr + mesh.attrib_data_stride * index));
        float3 world_pos = mul(float4(v_pos, 1.0), instance.transform).xyz;
        float3 world_normal = mul(float4(v_normal, 0.0), instance.transform).xyz;

        out_vertex.pos = mul(float4(world_pos, 1.0), g_constants.view_projection);
        out_vertex.normal = normalize(world_normal); //normalize(mul(float4(world_normal, 0.0), g_constants.view_projection)).xyz;
    }
    return out_vertex;
}

float4 fsMain(ClipVertex vertex) : SV_Target0
{
    float3 light_dir = float3(0, 1, 0);
    float n_dot_l = clamp(dot(vertex.normal, light_dir), 0.0, 1.0);
    return float4(float3(1, 1, 1) * n_dot_l, 1.0);
}
