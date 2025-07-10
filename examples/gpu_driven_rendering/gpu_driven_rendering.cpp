#include <array>
#include <brtoy/container.h>
#include <brtoy/gfx.h>
#include <brtoy/gfx_swapchain.h>
#include <brtoy/gfx_utils.h>
#include <brtoy/linmath.h>
#include <brtoy/platform.h>
#include <brtoy/vec.h>
#include <fstream>
#include <random>
#include <vk_mem_alloc.h>
#include <format>

namespace brtoy {

struct MeshInfo {
    uint32_t index_data_ptr;
    uint32_t pos_data_ptr;
    uint32_t pos_data_stride;
    uint32_t attrib_data_ptr;
    uint32_t attrib_data_stride;
    uint32_t index_count;
};

struct MeshData {
    using Index = uint32_t;
    static constexpr VkDeviceSize StagingBufferSize = 8 * 1024 * 1024;
    static constexpr VkDeviceSize PositionBufferSize = 16 * 1024 * 1024;
    static constexpr VkDeviceSize AttribBufferSize = 16 * 1024 * 1024;
    static constexpr uint32_t IndexCountMax = 1024 * 1024;
    static constexpr VkDeviceSize IndexBufferSize = sizeof(Index) * IndexCountMax;
    static constexpr VkDeviceSize InfoSize = sizeof(MeshInfo);
    static constexpr uint32_t MeshCountMax = 1024;
    static constexpr VkDeviceSize InfoBufferSize = InfoSize * MeshCountMax;

    struct Creator {
        uint32_t position_size;
        uint32_t attrib_size;
        uint32_t index_count;
        BufferSubAllocation src_positions;
        BufferSubAllocation src_attribs;
        BufferSubAllocation src_indices;

        Index *indices();
    };

    template <typename PosT, typename AttribT> struct CreatorT : public Creator {
        PosT *positions() { return (PosT *)src_positions.ptr(); }

        AttribT *attribs() { return (AttribT *)src_attribs.ptr(); }
    };

    MeshData(VmaAllocator allocator);
    ~MeshData();

    template <typename PosT, typename AttribT>
    CreatorT<PosT, AttribT> create(uint32_t vertex_count, uint32_t index_count) {
        CreatorT<PosT, AttribT> creator;
        creator.position_size = sizeof(PosT);
        creator.attrib_size = sizeof(AttribT);
        creator.index_count = index_count;
        creator.src_positions = m_staging.allocate<PosT>(vertex_count);
        creator.src_attribs = m_staging.allocate<AttribT>(vertex_count);
        creator.src_indices = m_staging.allocate<Index>(index_count);
        return creator;
    }

    uint32_t update(VkCommandBuffer cmd, const Creator &creator);

    VmaAllocator m_allocator;
    VkBuffer m_staging_buffer;
    VmaAllocation m_staging_allocation;
    VkBuffer m_buffer;
    VmaAllocation m_allocation;
    LinearAllocator m_staging;
    LinearAllocator m_positions;
    LinearAllocator m_attribs;
    LinearAllocator m_indices;
    LinearAllocator m_infos;
};

MeshData::Index *MeshData::Creator::indices() { return (Index *)src_indices.ptr(); }

MeshData::MeshData(VmaAllocator allocator) : m_allocator(allocator) {
    VkBufferCreateInfo staging_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = StagingBufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VmaAllocationCreateInfo staging_alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    };
    VmaAllocationInfo staging_info;
    VkResult result = vmaCreateBuffer(m_allocator, &staging_create_info, &staging_alloc_info,
                                      &m_staging_buffer, &m_staging_allocation, &staging_info);
    BRTOY_ASSERT(result == VK_SUCCESS);
    m_staging =
        LinearAllocator(m_staging_buffer, 0, StagingBufferSize, 4, staging_info.pMappedData);

    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = PositionBufferSize + AttribBufferSize + IndexBufferSize + InfoBufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    VmaAllocationCreateInfo buffer_alloc_info = {
        .flags = 0,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    vmaCreateBuffer(m_allocator, &buffer_create_info, &buffer_alloc_info, &m_buffer, &m_allocation,
                    nullptr);

    m_positions = LinearAllocator(m_buffer, 0, PositionBufferSize, 4);
    m_attribs = LinearAllocator(m_buffer, PositionBufferSize, AttribBufferSize, 4);
    m_indices =
        LinearAllocator(m_buffer, PositionBufferSize + AttribBufferSize, IndexBufferSize, 4);
    m_infos = LinearAllocator(m_buffer, PositionBufferSize + AttribBufferSize + IndexBufferSize,
                              InfoBufferSize, 4);
}

MeshData::~MeshData() {
    vmaDestroyBuffer(m_allocator, m_staging_buffer, m_staging_allocation);
    vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
}

uint32_t MeshData::update(VkCommandBuffer cmd, const Creator &creator) {
    BufferSubAllocation dst_positions = m_positions.allocateBytes(creator.src_positions.size);
    BufferSubAllocation dst_attribs = m_attribs.allocateBytes(creator.src_attribs.size);
    BufferSubAllocation dst_indices = m_indices.allocateBytes(creator.src_indices.size);

    BufferSubAllocation dst_info = m_infos.allocate<MeshInfo>();
    BufferSubAllocation src_info = m_staging.allocate<MeshInfo>();

    MeshInfo *info = (MeshInfo *)src_info.ptr();
    info->index_data_ptr = dst_indices.offset;
    info->pos_data_ptr = dst_positions.offset;
    info->pos_data_stride = creator.position_size;
    info->attrib_data_ptr = dst_attribs.offset;
    info->attrib_data_stride = creator.attrib_size;
    info->index_count = creator.index_count;

    {
        VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_NONE,
                                   VK_ACCESS_TRANSFER_WRITE_BIT};
        vkCmdPipelineBarrier(
            cmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    std::array copy_regions = std::to_array<VkBufferCopy>({
        {creator.src_positions.offset, dst_positions.offset, creator.src_positions.size},
        {creator.src_attribs.offset, dst_attribs.offset, creator.src_attribs.size},
        {creator.src_indices.offset, dst_indices.offset, creator.src_indices.size},
        {src_info.offset, dst_info.offset, src_info.size},
    });
    vkCmdCopyBuffer(cmd, m_staging_buffer, m_buffer, copy_regions.size(), copy_regions.data());

    {
        VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);
    }

    return dst_info.offset;
}

struct Instance {
    M44f transform;
    uint32_t mesh_info_ptr;
    uint32_t pad[3];
};
static_assert(sizeof(Instance) == 64 + 16);

struct World {
    MeshData &m_mesh_data;

    M44f m_view_proj;
    std::vector<Instance> m_instances;

    void addInstance(M44f transform, uint32_t mesh);
};

void World::addInstance(M44f transform, uint32_t mesh) {
    Instance instance;
    instance.transform = transpose(transform);
    instance.mesh_info_ptr = mesh;
    m_instances.push_back(std::move(instance));
}

struct RenderTarget {
    VkImageView color_view;
    VkImageView depth_view;
    VkImageView resolve_view;
    VkRect2D area;
};

struct WorldConstants {
    M44f view_proj;
};

struct Buffer {
    void free(VmaAllocator allocator);
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation mem = VK_NULL_HANDLE;
};

void Buffer::free(VmaAllocator allocator) {
    vmaDestroyBuffer(allocator, handle, mem);
    handle = VK_NULL_HANDLE;
    mem = VK_NULL_HANDLE;
}

struct DrawWorldPipeline {
    const GfxDevice &m_device;
    VmaAllocator m_allocator;
    const World &m_world;
    VkShaderModule m_cull_cs;
    VkShaderModule m_draw_vs;
    VkShaderModule m_draw_fs;
    VkDescriptorSetLayout m_cull_data_layout;
    VkDescriptorSetLayout m_mesh_data_layout;
    VkDescriptorSetLayout m_instance_data_layout;
    VkPipelineLayout m_cull_pipeline_layout;
    VkPipeline m_cull_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_draw_pipeline_layout;
    VkPipeline m_draw_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptor_pool;
    VkDescriptorSet m_mesh_data_descriptor_set;

    Buffer m_constants;
    Buffer m_instances;
    Buffer m_visible_instances;
    Buffer m_draw_cmds;
    Buffer m_readback;

    struct Frame {
        WorldConstants *constants;
        Instance *instances;
        VkDescriptorSet descriptor_set;
        VkDescriptorSet cull_descriptor_set;
        VkDrawIndirectCommand* draw_cmd_readback;
    };
    std::array<Frame, 3> m_frames;
    uint32_t m_frame_index = 0;

    DrawWorldPipeline(const GfxDevice &m_device, VmaAllocator allocator, const World &world);
    ~DrawWorldPipeline();

    uint32_t execute(VkCommandBuffer cmd, const RenderTarget &render_target);
};

static std::vector<std::byte> readEntireFile(const char *filename) {
    std::vector<std::byte> result;
    std::ifstream ifs(filename, std::ios::in | std::ios::binary);
    while (ifs) {
        std::array<std::byte, 128> buffer;
        ifs.read((char *)buffer.data(), buffer.size());
        if (ifs.gcount())
            result.insert(result.end(), buffer.begin(), buffer.begin() + ifs.gcount());
    }
    return result;
}

inline constexpr VkDeviceSize InstanceCountMax = 1000000;
inline constexpr VkDeviceSize InstancesBufferSize = sizeof(Instance) * InstanceCountMax;
inline constexpr VkDeviceSize VisibleInstancesBufferSize = sizeof(uint32_t) * InstanceCountMax;
inline constexpr VkDeviceSize ConstantBufferSize = sizeof(WorldConstants);
inline constexpr VkDeviceSize DrawCmdBufferSize = sizeof(VkDrawIndirectCommand);

DrawWorldPipeline::DrawWorldPipeline(const GfxDevice &device, VmaAllocator allocator,
                                     const World &world)
    : m_device(device), m_allocator(allocator), m_world(world) {
    VkResult result;

    auto cull_cs_code = readEntireFile("cull_instances.spv");
    struct VkShaderModuleCreateInfo cull_cs_create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = cull_cs_code.size(),
        .pCode = (uint32_t *)cull_cs_code.data(),
    };
    result = vkCreateShaderModule(m_device.m_device, &cull_cs_create_info, nullptr, &m_cull_cs);
    BRTOY_ASSERT(result == VK_SUCCESS);

    auto vs_code = readEntireFile("world_vs.spv");
    struct VkShaderModuleCreateInfo vs_create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = vs_code.size(),
        .pCode = (uint32_t *)vs_code.data(),
    };
    result = vkCreateShaderModule(m_device.m_device, &vs_create_info, nullptr, &m_draw_vs);
    BRTOY_ASSERT(result == VK_SUCCESS);

    auto fs_code = readEntireFile("world_fs.spv");
    struct VkShaderModuleCreateInfo fs_create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = fs_code.size(),
        .pCode = (uint32_t *)fs_code.data()};
    result = vkCreateShaderModule(m_device.m_device, &fs_create_info, nullptr, &m_draw_fs);
    BRTOY_ASSERT(result == VK_SUCCESS);

    std::array mesh_data_bindings = std::to_array<VkDescriptorSetLayoutBinding>({
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL,
            .pImmutableSamplers = nullptr,
        },
    });
    VkDescriptorSetLayoutCreateInfo mesh_data_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = mesh_data_bindings.size(),
        .pBindings = mesh_data_bindings.data(),
    };
    result = vkCreateDescriptorSetLayout(m_device.m_device, &mesh_data_layout_create_info, nullptr,
                                         &m_mesh_data_layout);
    BRTOY_ASSERT(result == VK_SUCCESS);

    std::array instance_data_bindings = std::to_array<VkDescriptorSetLayoutBinding>(
        {{
             .binding = 0,
             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount = 1,
             .stageFlags = VK_SHADER_STAGE_ALL,
             .pImmutableSamplers = nullptr,
         },
         {
             .binding = 1,
             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .descriptorCount = 1,
             .stageFlags = VK_SHADER_STAGE_ALL,
             .pImmutableSamplers = nullptr,
         },
         {
             .binding = 2,
             .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
             .descriptorCount = 1,
             .stageFlags = VK_SHADER_STAGE_ALL,
             .pImmutableSamplers = nullptr,
         }});
    VkDescriptorSetLayoutCreateInfo instance_data_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = instance_data_bindings.size(),
        .pBindings = instance_data_bindings.data(),
    };
    result = vkCreateDescriptorSetLayout(m_device.m_device, &instance_data_layout_create_info,
                                         nullptr, &m_instance_data_layout);
    BRTOY_ASSERT(result == VK_SUCCESS);

    std::array cull_data_bindings = std::to_array<VkDescriptorSetLayoutBinding>({
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        },
    });
    VkDescriptorSetLayoutCreateInfo cull_data_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = cull_data_bindings.size(),
        .pBindings = cull_data_bindings.data(),
    };
    result = vkCreateDescriptorSetLayout(m_device.m_device, &cull_data_layout_create_info, nullptr,
                                         &m_cull_data_layout);
    BRTOY_ASSERT(result == VK_SUCCESS);

    VkPipelineShaderStageCreateInfo cull_pipeline_shader_stage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = m_cull_cs,
        .pName = "cullInstances",
        .pSpecializationInfo = nullptr,
    };

    std::array cull_set_layouts = {m_mesh_data_layout, m_instance_data_layout, m_cull_data_layout};
    VkPipelineLayoutCreateInfo cull_pipeline_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = cull_set_layouts.size(),
        .pSetLayouts = cull_set_layouts.data(),
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };

    result = vkCreatePipelineLayout(m_device.m_device, &cull_pipeline_layout_create_info, nullptr,
                                    &m_cull_pipeline_layout);
    BRTOY_ASSERT(result == VK_SUCCESS);

    VkComputePipelineCreateInfo cull_pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = cull_pipeline_shader_stage,
        .layout = m_cull_pipeline_layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    };

    vkCreateComputePipelines(m_device.m_device, VK_NULL_HANDLE, 1, &cull_pipeline_create_info,
                             nullptr, &m_cull_pipeline);

    VkPipelineShaderStageCreateInfo vs_stage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = m_draw_vs,
        .pName = "vsMain",
        .pSpecializationInfo = nullptr,
    };
    VkPipelineShaderStageCreateInfo fs_stage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = m_draw_fs,
        .pName = "fsMain",
        .pSpecializationInfo = nullptr,
    };
    std::array stages = std::to_array({vs_stage, fs_stage});

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };
    VkPipelineTessellationStateCreateInfo tessellation_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .patchControlPoints = 0,
    };
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr,
    };
    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_8_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };
    VkBool32 blendEnable;
    VkBlendFactor srcColorBlendFactor;
    VkBlendFactor dstColorBlendFactor;
    VkBlendOp colorBlendOp;
    VkBlendFactor srcAlphaBlendFactor;
    VkBlendFactor dstAlphaBlendFactor;
    VkBlendOp alphaBlendOp;
    VkColorComponentFlags colorWriteMask;
    std::array blend_attachments = std::to_array<VkPipelineColorBlendAttachmentState>(
        {{VK_FALSE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
          VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
              VK_COLOR_COMPONENT_A_BIT}});
    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_CLEAR,
        .attachmentCount = blend_attachments.size(),
        .pAttachments = blend_attachments.data(),
        .blendConstants = {},
    };
    std::array dynamic_states = std::to_array<VkDynamicState>({
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    });
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = dynamic_states.size(),
        .pDynamicStates = dynamic_states.data(),
    };

    std::array set_layouts = std::to_array({m_mesh_data_layout, m_instance_data_layout});
    VkPipelineLayoutCreateInfo layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = set_layouts.size(),
        .pSetLayouts = set_layouts.data(),
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };
    result = vkCreatePipelineLayout(m_device.m_device, &layout_create_info, nullptr,
                                    &m_draw_pipeline_layout);
    BRTOY_ASSERT(result == VK_SUCCESS);

    VkGraphicsPipelineCreateInfo pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stageCount = stages.size(),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pTessellationState = &tessellation_state,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState = &multisample_state,
        .pDepthStencilState = &depth_stencil_state,
        .pColorBlendState = &color_blend_state,
        .pDynamicState = &dynamic_state,
        .layout = m_draw_pipeline_layout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    };

    std::array color_formats = {VK_FORMAT_B8G8R8A8_SRGB};
    VkPipelineRenderingCreateInfo rendering_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = pipeline_create_info.pNext,
        .viewMask = 0,
        .colorAttachmentCount = color_formats.size(),
        .pColorAttachmentFormats = color_formats.data(),
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
    };
    pipeline_create_info.pNext = &rendering_create_info;
    result = vkCreateGraphicsPipelines(m_device.m_device, VK_NULL_HANDLE, 1, &pipeline_create_info,
                                       nullptr, &m_draw_pipeline);
    BRTOY_ASSERT(result == VK_SUCCESS);

    std::array descriptor_set_layouts = {
        m_mesh_data_layout, m_instance_data_layout, m_cull_data_layout, m_instance_data_layout,
        m_cull_data_layout, m_instance_data_layout, m_cull_data_layout};
    constexpr uint32_t DescriptorSetCountMax = descriptor_set_layouts.size();

    std::array descriptor_pool_sizes = std::to_array<VkDescriptorPoolSize>({
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 + 3},
    });
    VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = DescriptorSetCountMax,
        .poolSizeCount = 0,
        .pPoolSizes = nullptr,
    };
    result = vkCreateDescriptorPool(m_device.m_device, &descriptor_pool_create_info, nullptr,
                                    &m_descriptor_pool);
    BRTOY_ASSERT(result == VK_SUCCESS);

    VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = m_descriptor_pool,
        .descriptorSetCount = descriptor_set_layouts.size(),
        .pSetLayouts = descriptor_set_layouts.data()};
    std::array<VkDescriptorSet, descriptor_set_layouts.size()> descriptor_sets;
    result = vkAllocateDescriptorSets(m_device.m_device, &descriptor_set_alloc_info,
                                      descriptor_sets.data());
    BRTOY_ASSERT(result == VK_SUCCESS);

    m_mesh_data_descriptor_set = descriptor_sets[0];
    VkDescriptorBufferInfo mesh_data_descriptor_info = {m_world.m_mesh_data.m_buffer, 0,
                                                        VK_WHOLE_SIZE};
    std::array mesh_descriptor_writes = std::to_array<VkWriteDescriptorSet>({
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_mesh_data_descriptor_set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &mesh_data_descriptor_info, nullptr},
    });
    vkUpdateDescriptorSets(m_device.m_device, mesh_descriptor_writes.size(),
                           mesh_descriptor_writes.data(), 0, nullptr);

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(m_device.m_physical_device, &physical_device_properties);
    VkDeviceSize cb_alignment = physical_device_properties.limits.minUniformBufferOffsetAlignment;
    const VkDeviceSize alignedConstantBufferSize = alignUp(ConstantBufferSize, cb_alignment);

    VkBufferCreateInfo constant_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = alignedConstantBufferSize * m_frames.size(),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    };
    VmaAllocationCreateInfo constants_allocation_create_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    };
    VmaAllocationInfo constants_allocation_info;
    vmaCreateBuffer(m_allocator, &constant_buffer_create_info, &constants_allocation_create_info,
                    &m_constants.handle, &m_constants.mem, &constants_allocation_info);

    VkBufferCreateInfo instances_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = InstancesBufferSize * m_frames.size(),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    VmaAllocationCreateInfo instances_allocation_create_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = 0,
    };
    VmaAllocationInfo instances_allocation_info;
    vmaCreateBuffer(m_allocator, &instances_buffer_create_info, &instances_allocation_create_info,
                    &m_instances.handle, &m_instances.mem, &instances_allocation_info);

    VkBufferCreateInfo visible_instances_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = VisibleInstancesBufferSize * m_frames.size(),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    VmaAllocationCreateInfo visible_instances_allocation_create_info = {
        .flags = 0,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = 0,
    };
    vmaCreateBuffer(m_allocator, &visible_instances_buffer_create_info,
                    &visible_instances_allocation_create_info, &m_visible_instances.handle,
                    &m_visible_instances.mem, nullptr);

    VkBufferCreateInfo draw_cmds_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = DrawCmdBufferSize * m_frames.size(),
        .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };
    VmaAllocationCreateInfo draw_cmds_allocation_create_info = {
        .flags = 0,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = 0,
    };
    vmaCreateBuffer(m_allocator, &draw_cmds_buffer_create_info, &draw_cmds_allocation_create_info,
                    &m_draw_cmds.handle, &m_draw_cmds.mem, nullptr);

    VkBufferCreateInfo readback_buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = DrawCmdBufferSize * m_frames.size(),
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VmaAllocationCreateInfo readback_allocation_create_info = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo readback_allocation_info;
    vmaCreateBuffer(m_allocator, &readback_buffer_create_info, &readback_allocation_create_info,
                    &m_readback.handle, &m_readback.mem, &readback_allocation_info);

    for (size_t i = 0; i < m_frames.size(); ++i) {
        Frame &frame = m_frames[i];
        frame.constants = (WorldConstants *)((uint8_t *)constants_allocation_info.pMappedData +
                                             alignedConstantBufferSize * i);
        frame.instances = (Instance *)((uint8_t *)instances_allocation_info.pMappedData +
                                       InstancesBufferSize * i);
        frame.draw_cmd_readback = (VkDrawIndirectCommand*)((uint8_t*)readback_allocation_info.pMappedData + DrawCmdBufferSize * i);

        VkDescriptorBufferInfo constant_descriptor_info = {
            m_constants.handle, alignedConstantBufferSize * i, alignedConstantBufferSize};
        VkDescriptorBufferInfo instance_descriptor_info = {
            m_instances.handle, InstancesBufferSize * i, InstancesBufferSize};
        VkDescriptorBufferInfo visible_instances_descriptor_info = {
            m_visible_instances.handle, VisibleInstancesBufferSize * i, VisibleInstancesBufferSize};
        VkDescriptorBufferInfo draw_cmd_descriptor_info = {
            m_draw_cmds.handle, DrawCmdBufferSize * i, DrawCmdBufferSize};

        frame.descriptor_set = descriptor_sets[1 + i * 2 + 0];
        frame.cull_descriptor_set = descriptor_sets[1 + i * 2 + 1];
        std::array descriptor_writes = std::to_array<VkWriteDescriptorSet>({
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, frame.descriptor_set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &instance_descriptor_info, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, frame.descriptor_set, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &visible_instances_descriptor_info,
             nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, frame.descriptor_set, 2, 0, 1,
             VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &constant_descriptor_info, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, frame.cull_descriptor_set, 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &draw_cmd_descriptor_info, nullptr},
        });
        vkUpdateDescriptorSets(m_device.m_device, descriptor_writes.size(),
                               descriptor_writes.data(), 0, nullptr);
    }
}

DrawWorldPipeline::~DrawWorldPipeline() {
    VkDevice dev = m_device.m_device;

    m_readback.free(m_allocator);
    m_draw_cmds.free(m_allocator);
    m_visible_instances.free(m_allocator);
    m_instances.free(m_allocator);
    m_constants.free(m_allocator);

    vkDestroyDescriptorPool(dev, m_descriptor_pool, nullptr);
    vkDestroyPipeline(dev, m_draw_pipeline, nullptr);
    vkDestroyPipelineLayout(dev, m_draw_pipeline_layout, nullptr);
    vkDestroyPipeline(dev, m_cull_pipeline, nullptr);
    vkDestroyPipelineLayout(dev, m_cull_pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(dev, m_cull_data_layout, nullptr);
    vkDestroyDescriptorSetLayout(dev, m_instance_data_layout, nullptr);
    vkDestroyDescriptorSetLayout(dev, m_mesh_data_layout, nullptr);
    vkDestroyShaderModule(dev, m_cull_cs, nullptr);
    vkDestroyShaderModule(dev, m_draw_vs, nullptr);
    vkDestroyShaderModule(dev, m_draw_fs, nullptr);
}

uint32_t DrawWorldPipeline::execute(VkCommandBuffer cmd, const RenderTarget &render_target) {
    uint32_t buffer_index = m_frame_index % m_frames.size();
    Frame &frame = m_frames[buffer_index];
    vkCmdFillBuffer(cmd, m_draw_cmds.handle, buffer_index * DrawCmdBufferSize, DrawCmdBufferSize,
                    0);

    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cull_pipeline);
    std::array cull_descriptor_sets = std::to_array(
        {m_mesh_data_descriptor_set, frame.descriptor_set, frame.cull_descriptor_set});
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cull_pipeline_layout, 0,
                            cull_descriptor_sets.size(), cull_descriptor_sets.data(), 0, nullptr);
    uint32_t thread_group_size = 256;
    uint32_t thread_group_count =
        (m_world.m_instances.size() + thread_group_size - 1) / thread_group_size;
    vkCmdDispatch(cmd, thread_group_count, 1, 1);

    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    VkClearValue color_clear;
    color_clear.color.float32[0] = 0.04f;
    color_clear.color.float32[1] = 0.04f;
    color_clear.color.float32[2] = 0.04f;
    color_clear.color.float32[3] = 0;
    VkRenderingAttachmentInfo color_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = render_target.color_view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
        .resolveImageView = render_target.resolve_view,
        .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = color_clear,
    };

    VkClearValue depth_clear;
    depth_clear.depthStencil.depth = 1.0f;
    VkRenderingAttachmentInfo depth_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = render_target.depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = depth_clear,
    };

    VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = render_target.area,
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
        .pDepthAttachment = &depth_attachment,
        .pStencilAttachment = nullptr,
    };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_draw_pipeline);
    vkCmdBeginRendering(cmd, &rendering_info);
    std::array viewports = std::to_array<VkViewport>(
        {{(float)render_target.area.offset.x, (float)render_target.area.offset.y,
          (float)render_target.area.extent.width, (float)render_target.area.extent.height, 0.0f,
          1.0f}});
    vkCmdSetViewport(cmd, 0, viewports.size(), viewports.data());
    std::array scissors = std::to_array({render_target.area});
    vkCmdSetScissor(cmd, 0, scissors.size(), scissors.data());
    std::array draw_descriptor_sets =
        std::to_array({m_mesh_data_descriptor_set, frame.descriptor_set});
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_draw_pipeline_layout, 0,
                            draw_descriptor_sets.size(), draw_descriptor_sets.data(), 0, nullptr);

    frame.constants->view_proj = transpose(m_world.m_view_proj);
    std::copy(m_world.m_instances.begin(), m_world.m_instances.end(), frame.instances);

    vkCmdDrawIndirect(cmd, m_draw_cmds.handle, buffer_index * sizeof(VkDrawIndirectCommand), 1,
                      sizeof(VkDrawIndirectCommand));
    vkCmdEndRendering(cmd);


    VkBufferCopy copy_region = {.srcOffset = buffer_index * sizeof(VkDrawIndirectCommand),
                                .dstOffset = buffer_index * sizeof(VkDrawIndirectCommand),
                                .size = sizeof(VkDrawIndirectCommand)};
    vkCmdCopyBuffer(cmd, m_draw_cmds.handle, m_readback.handle, 1, &copy_region);
    ++m_frame_index;

    return m_frames[m_frame_index % m_frames.size()].draw_cmd_readback->instanceCount;
}

struct VertexNormal {
    V3f normal;
};

static uint32_t createTriangleGeo(MeshData &mesh_data, VkCommandBuffer cmd) {
    auto mesh = mesh_data.create<V3f, VertexNormal>(3, 3);
    mesh.positions()[0] = {0.0f, 0.5f, 0.0f};
    mesh.positions()[1] = {-0.5f, -0.5f, 0.0f};
    mesh.positions()[2] = {0.5f, -0.5f, 0.0f};
    mesh.indices()[0] = 0;
    mesh.indices()[1] = 1;
    mesh.indices()[2] = 2;
    return mesh_data.update(cmd, mesh);
}

static uint32_t createDiskGeo(MeshData &mesh_data, VkCommandBuffer cmd) {
    constexpr uint32_t SegmentCount = 40;
    auto mesh = mesh_data.create<V3f, VertexNormal>(SegmentCount + 1, SegmentCount * 3);
    mesh.positions()[0] = {0.0f, 0.0f, 0.0f};
    for (uint32_t i = 0; i < SegmentCount; ++i) {
        float angle = TwoPi * (float)i / (float)SegmentCount;
        uint16_t i0 = i + 1;
        uint16_t i1 = i < SegmentCount - 1 ? i + 2 : 1;
        uint16_t i2 = 0;
        mesh.positions()[i0] = {cosf(angle) * 0.5f, sinf(angle) * 0.5f, 0.0f};
        mesh.indices()[i * 3 + 0] = i0;
        mesh.indices()[i * 3 + 1] = i1;
        mesh.indices()[i * 3 + 2] = i2;
    }
    return mesh_data.update(cmd, mesh);
}

static uint32_t createTetrahedron(MeshData &mesh_data, VkCommandBuffer cmd) {
    auto mesh = mesh_data.create<V3f, VertexNormal>(12, 12);
    std::array p = std::to_array<V3f>({
        {0.0f, -0.5f, 0.5f},
        {-0.5f, -0.5f, -0.5f},
        {0.5f, -0.5f, -0.5f},
        {0.0f, 0.5f, 0.0f},
    });

    std::array positions = {
        p[0], p[1], p[2], p[0], p[3], p[1], p[1], p[3], p[2], p[2], p[3], p[0],
    };
    std::copy(positions.begin(), positions.end(), mesh.positions());

    std::array n = {
        normalize(cross(p[1] - p[0], p[2] - p[0])),
        normalize(cross(p[3] - p[0], p[1] - p[0])),
        normalize(cross(p[3] - p[1], p[2] - p[1])),
        normalize(cross(p[3] - p[2], p[0] - p[2])),
    };
    std::array attribs = std::to_array<VertexNormal>({
        {n[0]},
        {n[0]},
        {n[0]},
        {n[1]},
        {n[1]},
        {n[1]},
        {n[2]},
        {n[2]},
        {n[2]},
        {n[3]},
        {n[3]},
        {n[3]},
    });
    std::copy(attribs.begin(), attribs.end(), mesh.attribs());

    std::array indices = std::to_array<uint16_t>({
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
    });
    std::copy(indices.begin(), indices.end(), mesh.indices());
    return mesh_data.update(cmd, mesh);
}

static void populateWorld(const GfxDevice &device, CommandBufferPool &cb_pool, VkFence fence,
                          World &world) {
    std::mt19937 rng;
    std::normal_distribution<float> pos_distribution(0.0f, 200.0f);
    std::uniform_real_distribution<float> angle_distribution(0.0f, TwoPi);
    std::uniform_real_distribution<float> z_distribution(-1.0f, 1.0f);

    VkCommandBuffer cmd = cb_pool.acquire();
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                           VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd, &begin_info);

    uint32_t triangle_geo = createTriangleGeo(world.m_mesh_data, cmd);
    uint32_t disk_geo = createDiskGeo(world.m_mesh_data, cmd);
    uint32_t tet_geo = createTetrahedron(world.m_mesh_data, cmd);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    vkEndCommandBuffer(cmd);
    vkQueueSubmit(device.m_queue, 1, &submit_info, fence);
    cb_pool.release(cmd, fence);

    for (int i = 0; i < InstanceCountMax; ++i) {
        float theta = angle_distribution(rng);
        float z = z_distribution(rng);
        float zz = std::sqrtf(1 - z * z);
        V3f x_axis = {zz * std::cosf(theta), zz * std::sinf(theta), z};
        BRTOY_ASSERT(length(x_axis) < 1.0001f && length(x_axis) > 0.9999f);
        V3f u = {0.0f, 1.0f, 0.0f};
        if (std::fabsf(dot(x_axis, u)) < 0.0001f)
            u = {1.0f, 0.0f, 0.0f};
        V3f z_axis = normalize(cross(x_axis, u));
        BRTOY_ASSERT(length(z_axis) < 1.0001f && length(z_axis) > 0.9999f);
        V3f y_axis = normalize(cross(z_axis, x_axis));
        BRTOY_ASSERT(length(y_axis) < 1.0001f && length(y_axis) > 0.9999f);
        BRTOY_ASSERT(fabsf(dot(x_axis, y_axis)) < 0.0001f);
        BRTOY_ASSERT(fabsf(dot(x_axis, z_axis)) < 0.0001f);
        BRTOY_ASSERT(fabsf(dot(y_axis, z_axis)) < 0.0001f);
        V3f translation = {pos_distribution(rng), pos_distribution(rng), pos_distribution(rng)};

        M44f transform = {
            V4f{x_axis.x, x_axis.y, x_axis.z, 0},
            V4f{y_axis.x, y_axis.y, y_axis.z, 0},
            V4f{z_axis.x, z_axis.y, z_axis.z, 0},
            V4f{translation.x, translation.y, translation.z, 1},
        };

        world.addInstance(transform, tet_geo);
    }
}

struct GfxContext {
    static std::optional<GfxContext> create();
    GfxContext(GfxInstance &&instance, GfxDevice &&device, VmaAllocator allocator);
    GfxContext(GfxContext &&) = default;
    ~GfxContext();

    GfxInstance m_instance;
    GfxDevice m_device;
    VmaAllocator m_memory_allocator;
};

GfxContext::GfxContext(GfxInstance &&instance, GfxDevice &&device, VmaAllocator allocator)
    : m_instance(std::move(instance)), m_device(std::move(device)), m_memory_allocator(allocator) {}

std::optional<GfxContext> GfxContext::create() {
    std::optional<GfxContext> ctx;
    auto instance = GfxInstance::create("gpu_driven_rendering", 0, GfxDebugFlag::ValidationEnable);
    if (instance) {
        auto device = GfxDevice::createDefault(*instance);
        if (device) {
            VmaAllocatorCreateInfo create_info = {
                .flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
                .physicalDevice = device->m_physical_device,
                .device = device->m_device,
                .preferredLargeHeapBlockSize = 0,
                .pAllocationCallbacks = nullptr,
                .pDeviceMemoryCallbacks = nullptr,
                .pHeapSizeLimit = nullptr,
                .pVulkanFunctions = nullptr,
                .instance = instance->m_instance,
                .vulkanApiVersion = instance->m_api_version,
                .pTypeExternalMemoryHandleTypes = nullptr,
            };

            VmaAllocator allocator;
            VkResult result = vmaCreateAllocator(&create_info, &allocator);
            if (result == VK_SUCCESS) {
                ctx.emplace(std::move(instance.value()), std::move(device.value()), allocator);
            } else {
                Platform::errorMessage("Could not create graphics memory allocator");
                ctx = std::nullopt;
            }
        } else {
            Platform::errorMessage("Could not create graphics device");
            ctx = std::nullopt;
        }
    } else {
        Platform::errorMessage("Could not create graphics instance");
        ctx = std::nullopt;
    }
    return ctx;
}

GfxContext::~GfxContext() {
    if (m_memory_allocator != VK_NULL_HANDLE)
        vmaDestroyAllocator(m_memory_allocator);
}

int runExample() {
    auto platform = Platform::init();
    if (!platform) {
        Platform::errorMessage("Could not initialize platform layer");
        return -1;
    }

    Window window = platform->createWindow("Example - GPU Driven Rendering");
    if (!window) {
        Platform::errorMessage("Could not create window.");
        return -1;
    }

    auto ctx = GfxContext::create();
    if (!ctx) {
        return -1;
    }

    WindowState window_state = platform->windowState(window);

    Swapchain swapchain(ctx->m_instance, platform->appInstanceHandle(), window_state.native_handle,
                        ctx->m_device.m_physical_device, ctx->m_device.m_device);
    std::optional<Backbuffer> backbuffer;

    VkSemaphoreCreateInfo sem_create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
    VkSemaphore begin_sem, end_sem;
    vkCreateSemaphore(ctx->m_device.m_device, &sem_create_info, nullptr, &begin_sem);
    vkCreateSemaphore(ctx->m_device.m_device, &sem_create_info, nullptr, &end_sem);

    CommandBufferPool cb_pool(ctx->m_device);

    VkImageCreateInfo color_image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = swapchain.m_format.format,
        .extent = {},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_8_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImageViewCreateInfo color_view_create_info = {.sType =
                                                        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                                    .pNext = nullptr,
                                                    .flags = 0,
                                                    .image = VK_NULL_HANDLE,
                                                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                                    .format = color_image_create_info.format,
                                                    .components = {},
                                                    .subresourceRange = {
                                                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                        .baseMipLevel = 0,
                                                        .levelCount = 1,
                                                        .baseArrayLayer = 0,
                                                        .layerCount = 1,
                                                    }};
    VkImageMemoryBarrier color_init_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0,
        .dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = 0,
        .dstQueueFamilyIndex = 0,
        .image = VK_NULL_HANDLE,
        .subresourceRange = color_view_create_info.subresourceRange,
    };
    TexturePool color_texture_pool(ctx->m_device, ctx->m_memory_allocator, color_image_create_info,
                                   color_view_create_info, color_init_barrier,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkImageCreateInfo depth_image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = {},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_8_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage =
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImageViewCreateInfo depth_view_create_info = {.sType =
                                                        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                                    .pNext = nullptr,
                                                    .flags = 0,
                                                    .image = VK_NULL_HANDLE,
                                                    .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                                    .format = depth_image_create_info.format,
                                                    .components = {},
                                                    .subresourceRange = {
                                                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                                        .baseMipLevel = 0,
                                                        .levelCount = 1,
                                                        .baseArrayLayer = 0,
                                                        .layerCount = 1,
                                                    }};
    VkImageMemoryBarrier depth_init_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = 0,
        .dstQueueFamilyIndex = 0,
        .image = VK_NULL_HANDLE,
        .subresourceRange = depth_view_create_info.subresourceRange,
    };
    TexturePool ds_pool(ctx->m_device, ctx->m_memory_allocator, depth_image_create_info,
                        depth_view_create_info, depth_init_barrier,
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

    MeshData mesh_data(ctx->m_memory_allocator);
    World world{mesh_data};

    VkFence init_fence;
    VkFenceCreateInfo init_fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(ctx->m_device.m_device, &init_fence_info, nullptr, &init_fence);
    populateWorld(ctx->m_device, cb_pool, init_fence, world);

    DrawWorldPipeline world_pipeline(ctx->m_device, ctx->m_memory_allocator, world);

    auto synchronizePools = [&]() {
        cb_pool.sync();
        color_texture_pool.sync();
        ds_pool.sync();
    };

    float view_a = 0.0f;
    float yaw = TwoPi * 0.5f;
    float pitch = 0.0f;
    V3f look_p = {0.0f, 0.0f, 0.0f};
    V3f cam_p = {0.0f, 0.0f, -3.0f};
    Input input;
    while (platform->tick(input)) {
        u64 start_timestamp = platform->getTimestamp();
        window_state = platform->windowState(window);
        if (window_state.is_closing) {
            platform->requestQuit();
            continue;
        }

        if (window_state.dim != swapchain.m_dim) {
            vkDeviceWaitIdle(ctx->m_device.m_device);
            synchronizePools();
            backbuffer.reset();
            swapchain.recreate(window_state.dim);
            backbuffer = Backbuffer::createFromSwapchain(ctx->m_device.m_device, swapchain);
        }

        if (!backbuffer)
            continue;

        u32 image_index;
        vkAcquireNextImageKHR(ctx->m_device.m_device, swapchain.m_swapchain, UINT64_MAX, begin_sem,
                              VK_NULL_HANDLE, &image_index);
        const Backbuffer::Buffer &current_buffer = backbuffer->m_buffers[image_index];

        vkWaitForFences(ctx->m_device.m_device, 1, &current_buffer.fence, VK_TRUE, UINT64_MAX);
        synchronizePools();
        vkResetFences(ctx->m_device.m_device, 1, &current_buffer.fence);

        M44f cam;
        setTranslate(cam, cam_p);
        if (input.lmb_is_down) {
            constexpr float look_speed = 0.005f;
            yaw += input.mouse_dx * -look_speed;
            pitch = std::max(-HalfPi, std::min(pitch + input.mouse_dy * -look_speed, HalfPi));
        }
        rotateY(cam, yaw);
        rotateX(cam, pitch);
        if (input.lmb_is_down) {
            float move_speed = 0.1f;
            if (input.key_is_down[0xA0]) { // VK_LSHIFT
                move_speed *= 10;
            }
            if (input.key_is_down['W']) {
                cam_p += V3f{cam.k.x, cam.k.y, cam.k.z} * -move_speed;
            }
            if (input.key_is_down['A']) {
                cam_p += V3f{cam.i.x, cam.i.y, cam.i.z} * -move_speed;
            }
            if (input.key_is_down['S']) {
                cam_p += V3f{cam.k.x, cam.k.y, cam.k.z} * move_speed;
            }
            if (input.key_is_down['D']) {
                cam_p += V3f{cam.i.x, cam.i.y, cam.i.z} * move_speed;
            }
        }

        M44f view = invert(cam);
        float aspect_ratio = float(backbuffer->m_dim.x) / float(backbuffer->m_dim.y);
        M44f proj = perspectiveProjection(toRadians(45.0f), aspect_ratio, 0.1f, 1000.0f);
        world.m_view_proj = proj * view;

        VkCommandBuffer cmd = cb_pool.acquire();
        VkCommandBufferBeginInfo cmd_begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr};
        vkBeginCommandBuffer(cmd, &cmd_begin_info);

        {
            VkImageMemoryBarrier image_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_NONE,
                .dstAccessMask =
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = ctx->m_device.m_queue_family_index,
                .dstQueueFamilyIndex = ctx->m_device.m_queue_family_index,
                .image = current_buffer.image,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &image_barrier);
        }

        auto color_texture = color_texture_pool.acquire(cmd, backbuffer->m_dim);
        auto depth_stencil = ds_pool.acquire(cmd, backbuffer->m_dim);
        RenderTarget render_target{
            .color_view = color_texture.view,
            .depth_view = depth_stencil.view,
            .resolve_view = current_buffer.view,
            .area = {.offset = {0, 0}, .extent = {backbuffer->m_dim.x, backbuffer->m_dim.y}},
        };
        uint32_t instance_count = world_pipeline.execute(cmd, render_target);
        std::string window_title = std::format("Example - GPU Driven Rendering -- (lclick+drag to look, lclick+wasd to move) -- visible instances: {}/{}", instance_count, InstanceCountMax);
        platform->setWindowTitle(window, window_title);

        ds_pool.release(cmd, depth_stencil, current_buffer.fence);
        color_texture_pool.release(cmd, color_texture, current_buffer.fence);

        {
            std::array image_barriers = {
                VkImageMemoryBarrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_NONE,
                    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    .srcQueueFamilyIndex = ctx->m_device.m_queue_family_index,
                    .dstQueueFamilyIndex = ctx->m_device.m_queue_family_index,
                    .image = current_buffer.image,
                    .subresourceRange =
                        {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                        },
                },
            };

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
                                 image_barriers.size(), image_barriers.data());
        }
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags sem_wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &begin_sem,
            .pWaitDstStageMask = &sem_wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &end_sem,
        };
        vkQueueSubmit(ctx->m_device.m_queue, 1, &submit_info, current_buffer.fence);
        cb_pool.release(cmd, current_buffer.fence);

        VkPresentInfoKHR present_info = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                         .pNext = nullptr,
                                         .waitSemaphoreCount = 1,
                                         .pWaitSemaphores = &end_sem,
                                         .swapchainCount = 1,
                                         .pSwapchains = &swapchain.m_swapchain,
                                         .pImageIndices = &image_index,
                                         .pResults = nullptr};
        vkQueuePresentKHR(ctx->m_device.m_queue, &present_info);
    }

    vkDestroyFence(ctx->m_device.m_device, init_fence, nullptr);

    VkFence flush_fence;
    VkFenceCreateInfo fence_create_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
    vkCreateFence(ctx->m_device.m_device, &fence_create_info, nullptr, &flush_fence);
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 0,
        .pCommandBuffers = nullptr,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    vkQueueSubmit(ctx->m_device.m_queue, 1, &submit_info, flush_fence);
    vkWaitForFences(ctx->m_device.m_device, 1, &flush_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(ctx->m_device.m_device, flush_fence, nullptr);

    vkDestroySemaphore(ctx->m_device.m_device, begin_sem, nullptr);
    vkDestroySemaphore(ctx->m_device.m_device, end_sem, nullptr);

    return 0;
}

} // namespace brtoy

int main(int argc, char **argv) { return brtoy::runExample(); }
