#if defined(TILES)

#include "r3d_gpu.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "debug.h"
#include "path_info.h"

namespace r3d
{

namespace
{

std::vector<unsigned char> read_file_bytes( const std::string &path )
{
    std::ifstream in( path, std::ios::binary );
    if( !in.is_open() ) {
        return {};
    }
    return { std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
}

struct shader_format {
    SDL_GPUShaderFormat flag;
    const char *suffix;
};

// Same preference order and artifact naming as cata_shader.cpp (built by tools/build_shaders.py).
constexpr shader_format shader_formats[] = {
    { SDL_GPU_SHADERFORMAT_SPIRV, ".spv" },
    { SDL_GPU_SHADERFORMAT_DXIL, ".dxil" },
    { SDL_GPU_SHADERFORMAT_MSL, ".msl" },
};

} // namespace

gpu::~gpu()
{
    shutdown();
}

SDL_GPUShader *gpu::load_shader( const char *basename, SDL_GPUShaderStage stage,
                                 uint32_t samplers, uint32_t uniform_buffers )
{
    const SDL_GPUShaderFormat available = SDL_GetGPUShaderFormats( device_ );
    const shader_format *chosen = nullptr;
    for( const shader_format &f : shader_formats ) {
        if( ( available & f.flag ) != 0 ) {
            chosen = &f;
            break;
        }
    }
    if( !chosen ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: no shader format of the GPU device is shipped";
        return nullptr;
    }
    const std::string path = PATH_INFO::datadir() + "shaders/" + basename + chosen->suffix;
    const std::vector<unsigned char> bytes = read_file_bytes( path );
    if( bytes.empty() ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: cannot read shader " << path;
        return nullptr;
    }
    SDL_GPUShaderCreateInfo info{};
    info.code_size = bytes.size();
    info.code = bytes.data();
    info.entrypoint = nullptr;
    info.format = chosen->flag;
    info.stage = stage;
    info.num_samplers = samplers;
    info.num_uniform_buffers = uniform_buffers;
    SDL_GPUShader *shader = SDL_CreateGPUShader( device_, &info );
    if( !shader ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: SDL_CreateGPUShader(" << path << ") failed: "
                                    << SDL_GetError();
    }
    return shader;
}

bool gpu::init( SDL_Renderer *renderer )
{
    shutdown();
    renderer_ = renderer;
    device_ = renderer ? SDL_GetGPURendererDevice( renderer ) : nullptr;
    if( !device_ ) {
        renderer_ = nullptr;
        return false;
    }

    // Prefer a 32-bit float depth buffer, fall back to what the device supports.
    for( SDL_GPUTextureFormat fmt : { SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM,
                                      SDL_GPU_TEXTUREFORMAT_D16_UNORM
                                    } ) {
        if( SDL_GPUTextureSupportsFormat( device_, fmt, SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET ) ) {
            depth_format_ = fmt;
            break;
        }
    }
    if( depth_format_ == SDL_GPU_TEXTUREFORMAT_INVALID ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: no usable depth format";
        shutdown();
        return false;
    }

    SDL_GPUShader *vs = load_shader( "r3d_block.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1 );
    SDL_GPUShader *fs = load_shader( "r3d_block.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0 );
    if( !vs || !fs ) {
        if( vs ) {
            SDL_ReleaseGPUShader( device_, vs );
        }
        if( fs ) {
            SDL_ReleaseGPUShader( device_, fs );
        }
        shutdown();
        return false;
    }

    SDL_GPUVertexBufferDescription buffers[2]{};
    buffers[0].slot = 0;
    buffers[0].pitch = sizeof( vertex );
    buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    buffers[1].slot = 1;
    buffers[1].pitch = sizeof( uint32_t );
    buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = 3 * sizeof( float );
    attributes[2].location = 2;
    attributes[2].buffer_slot = 1;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attributes[2].offset = 0;

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.vertex_input_state.vertex_buffer_descriptions = buffers;
    pci.vertex_input_state.num_vertex_buffers = 2;
    pci.vertex_input_state.vertex_attributes = attributes;
    pci.vertex_input_state.num_vertex_attributes = 3;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.target_info.color_target_descriptions = &color_target;
    pci.target_info.num_color_targets = 1;
    pci.target_info.depth_stencil_format = depth_format_;
    pci.target_info.has_depth_stencil_target = true;
    block_pipeline_ = SDL_CreateGPUGraphicsPipeline( device_, &pci );
    SDL_ReleaseGPUShader( device_, vs );
    SDL_ReleaseGPUShader( device_, fs );
    if( !block_pipeline_ ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: SDL_CreateGPUGraphicsPipeline failed: " << SDL_GetError();
        shutdown();
        return false;
    }

    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_NEAREST;
    sci.mag_filter = SDL_GPU_FILTER_NEAREST;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler( device_, &sci );
    if( !sampler_ ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: SDL_CreateGPUSampler failed: " << SDL_GetError();
        shutdown();
        return false;
    }
    return true;
}

void gpu::release_staging()
{
    if( staging_ && device_ ) {
        SDL_ReleaseGPUTransferBuffer( device_, staging_ );
    }
    staging_ = nullptr;
    staging_size_ = 0;
}

void gpu::release_target()
{
    if( wrapped_ ) {
        SDL_DestroyTexture( wrapped_ );
        wrapped_ = nullptr;
    }
    if( device_ ) {
        if( color_ ) {
            SDL_ReleaseGPUTexture( device_, color_ );
        }
        if( depth_ ) {
            SDL_ReleaseGPUTexture( device_, depth_ );
        }
    }
    color_ = nullptr;
    depth_ = nullptr;
    target_w_ = 0;
    target_h_ = 0;
}

void gpu::abort_frame()
{
    // A command buffer cannot be dropped, so finish whatever is open and submit it empty.
    if( pass_ ) {
        SDL_EndGPURenderPass( pass_ );
        pass_ = nullptr;
    }
    if( copy_pass_ ) {
        SDL_EndGPUCopyPass( copy_pass_ );
        copy_pass_ = nullptr;
    }
    if( cmd_ ) {
        SDL_SubmitGPUCommandBuffer( cmd_ );
        cmd_ = nullptr;
    }
    pending_.clear();
    pending_data_.clear();
}

void gpu::shutdown()
{
    if( device_ ) {
        abort_frame();
        // The renderer may still sample the wrapped target; make sure no GPU work references it.
        SDL_WaitForGPUIdle( device_ );
        release_target();
        release_staging();
        if( block_pipeline_ ) {
            SDL_ReleaseGPUGraphicsPipeline( device_, block_pipeline_ );
        }
        if( sampler_ ) {
            SDL_ReleaseGPUSampler( device_, sampler_ );
        }
    }
    block_pipeline_ = nullptr;
    sampler_ = nullptr;
    device_ = nullptr;
    renderer_ = nullptr;
    depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
}

bool gpu::ensure_target( int width, int height )
{
    if( !device_ || width <= 0 || height <= 0 ) {
        return false;
    }
    if( color_ && target_w_ == width && target_h_ == height ) {
        return true;
    }
    if( color_ ) {
        SDL_WaitForGPUIdle( device_ );
    }
    release_target();

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = static_cast<Uint32>( width );
    tci.height = static_cast<Uint32>( height );
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    color_ = SDL_CreateGPUTexture( device_, &tci );
    tci.format = depth_format_;
    tci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth_ = SDL_CreateGPUTexture( device_, &tci );
    if( !color_ || !depth_ ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: creating the " << width << "x" << height
                                    << " target failed: " << SDL_GetError();
        release_target();
        return false;
    }

    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty( props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, color_ );
    SDL_SetNumberProperty( props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32 );
    SDL_SetNumberProperty( props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET );
    SDL_SetNumberProperty( props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width );
    SDL_SetNumberProperty( props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height );
    wrapped_ = SDL_CreateTextureWithProperties( renderer_, props );
    SDL_DestroyProperties( props );
    if( !wrapped_ ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: wrapping the target as SDL_Texture failed: "
                                    << SDL_GetError();
        release_target();
        return false;
    }
    SDL_SetTextureBlendMode( wrapped_, SDL_BLENDMODE_NONE );
    // Linear, so a supersampled target (see r3d::draw) is averaged down instead of aliasing.
    SDL_SetTextureScaleMode( wrapped_, SDL_SCALEMODE_LINEAR );
    target_w_ = width;
    target_h_ = height;
    return true;
}

SDL_GPUBuffer *gpu::create_buffer( SDL_GPUBufferUsageFlags usage, uint32_t size )
{
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    return SDL_CreateGPUBuffer( device_, &info );
}

void gpu::release_buffer( SDL_GPUBuffer *buffer )
{
    if( buffer && device_ ) {
        SDL_ReleaseGPUBuffer( device_, buffer );
    }
}

bool gpu::begin_frame()
{
    if( !ready() || !color_ || cmd_ ) {
        return false;
    }
    if( !SDL_FlushRenderer( renderer_ ) ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: SDL_FlushRenderer failed: " << SDL_GetError();
        return false;
    }
    cmd_ = SDL_AcquireGPUCommandBuffer( device_ );
    return cmd_ != nullptr;
}

bool gpu::upload( SDL_GPUBuffer *dst, const void *data, uint32_t size, bool cycle )
{
    if( !cmd_ || pass_ || !dst || size == 0 ) {
        return false;
    }
    const size_t offset = pending_data_.size();
    const uint8_t *bytes = static_cast<const uint8_t *>( data );
    pending_data_.insert( pending_data_.end(), bytes, bytes + size );
    pending_.push_back( { dst, static_cast<uint32_t>( offset ), size, cycle } );
    return true;
}

bool gpu::flush_uploads()
{
    if( pending_.empty() ) {
        return true;
    }
    const uint32_t total = static_cast<uint32_t>( pending_data_.size() );
    bool ok = true;
    if( !staging_ || staging_size_ < total ) {
        release_staging();
        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size = total + total / 2;
        staging_ = SDL_CreateGPUTransferBuffer( device_, &tbi );
        staging_size_ = staging_ ? tbi.size : 0;
        ok = staging_ != nullptr;
    }
    void *mapped = ok ? SDL_MapGPUTransferBuffer( device_, staging_, true ) : nullptr;
    if( mapped ) {
        std::memcpy( mapped, pending_data_.data(), total );
        SDL_UnmapGPUTransferBuffer( device_, staging_ );
        copy_pass_ = SDL_BeginGPUCopyPass( cmd_ );
    }
    if( !mapped || !copy_pass_ ) {
        DebugLog( D_ERROR, DC_ALL ) << "r3d: staging upload failed: " << SDL_GetError();
        ok = false;
    } else {
        for( const pending_upload &u : pending_ ) {
            SDL_GPUTransferBufferLocation src{};
            src.transfer_buffer = staging_;
            src.offset = u.offset;
            SDL_GPUBufferRegion region{};
            region.buffer = u.dst;
            region.size = u.size;
            SDL_UploadToGPUBuffer( copy_pass_, &src, &region, u.cycle );
        }
        SDL_EndGPUCopyPass( copy_pass_ );
        copy_pass_ = nullptr;
    }
    pending_.clear();
    pending_data_.clear();
    return ok;
}

bool gpu::begin_pass( float r, float g, float b )
{
    if( !cmd_ || pass_ || !flush_uploads() ) {
        return false;
    }
    SDL_GPUColorTargetInfo cti{};
    cti.texture = color_;
    cti.clear_color = SDL_FColor{ r, g, b, 1.0f };
    cti.load_op = SDL_GPU_LOADOP_CLEAR;
    cti.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo dti{};
    dti.texture = depth_;
    dti.clear_depth = 1.0f;
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    pass_ = SDL_BeginGPURenderPass( cmd_, &cti, 1, &dti );
    return pass_ != nullptr;
}

void gpu::bind_block_pipeline()
{
    if( pass_ ) {
        SDL_BindGPUGraphicsPipeline( pass_, block_pipeline_ );
    }
}

void gpu::draw_block( SDL_GPUBuffer *positions, SDL_GPUBuffer *lights, SDL_GPUBuffer *indices,
                      uint32_t first_index, uint32_t index_count, SDL_Texture *sheet,
                      const block_uniforms &uniforms )
{
    if( !pass_ || !positions || !lights || !indices || index_count == 0 || !sheet ) {
        return;
    }
    const SDL_PropertiesID props = SDL_GetTextureProperties( sheet );
    SDL_GPUTexture *gpu_sheet = static_cast<SDL_GPUTexture *>(
                                    SDL_GetPointerProperty( props, SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr ) );
    if( !gpu_sheet ) {
        return;
    }
    const SDL_GPUBufferBinding vertex_bindings[2] = { { positions, 0 }, { lights, 0 } };
    SDL_BindGPUVertexBuffers( pass_, 0, vertex_bindings, 2 );
    const SDL_GPUBufferBinding index_binding = { indices, 0 };
    SDL_BindGPUIndexBuffer( pass_, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT );
    const SDL_GPUTextureSamplerBinding sampler_binding = { gpu_sheet, sampler_ };
    SDL_BindGPUFragmentSamplers( pass_, 0, &sampler_binding, 1 );
    SDL_PushGPUVertexUniformData( cmd_, 0, &uniforms, sizeof( uniforms ) );
    SDL_DrawGPUIndexedPrimitives( pass_, index_count, 1, first_index, 0, 0 );
}

SDL_Texture *gpu::end_frame()
{
    if( !cmd_ ) {
        return nullptr;
    }
    if( pass_ ) {
        SDL_EndGPURenderPass( pass_ );
        pass_ = nullptr;
    }
    if( copy_pass_ ) {
        SDL_EndGPUCopyPass( copy_pass_ );
        copy_pass_ = nullptr;
    }
    const bool ok = SDL_SubmitGPUCommandBuffer( cmd_ );
    cmd_ = nullptr;
    pending_.clear();
    pending_data_.clear();
    return ok ? wrapped_ : nullptr;
}

} // namespace r3d

#endif // defined(TILES)
