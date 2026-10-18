// Draws one quad through the compiled r3d_block shaders, exactly as the game sets up the
// pipeline, and checks the cutaway: with a cut circle around the centre the centre pixel must
// show the clear colour and the corners the quad; with the cut off the centre shows the quad.
// Built and run by run_shader_test.sh; exits 0 when both hold.
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>
struct vertex {
    float x, y, z, u, v;
};
struct block_uniforms {
    float m[16];
    float offset[4];
    float pull[4];
    float cut[4];
    float cut2[4];
};
static SDL_GPUShader *load( SDL_GPUDevice *d, const char *path, SDL_GPUShaderStage st,
                            Uint32 samplers, Uint32 ubos )
{
    size_t n = 0;
    void *data = SDL_LoadFile( path, &n );
    SDL_GPUShaderCreateInfo i{};
    i.code = ( const Uint8 * )data;
    i.code_size = n;
    i.entrypoint = "main";
    i.format = SDL_GPU_SHADERFORMAT_SPIRV;
    i.stage = st;
    i.num_samplers = samplers;
    i.num_uniform_buffers = ubos;
    SDL_GPUShader *s = SDL_CreateGPUShader( d, &i );
    if( !s ) {
        printf( "shader %s: %s\n", path, SDL_GetError() );
    }
    return s;
}
static SDL_GPUBuffer *upload( SDL_GPUDevice *d, SDL_GPUCopyPass *cp, SDL_GPUBufferUsageFlags usage,
                              const void *data, Uint32 size )
{
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = usage;
    bi.size = size;
    SDL_GPUBuffer *b = SDL_CreateGPUBuffer( d, &bi );
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    SDL_GPUTransferBuffer *t = SDL_CreateGPUTransferBuffer( d, &ti );
    void *m = SDL_MapGPUTransferBuffer( d, t, false );
    memcpy( m, data, size );
    SDL_UnmapGPUTransferBuffer( d, t );
    SDL_GPUTransferBufferLocation src{ t, 0 };
    SDL_GPUBufferRegion dst{ b, 0, size };
    SDL_UploadToGPUBuffer( cp, &src, &dst, false );
    return b;
}
int main( int argc, char **argv )
{
    SDL_Init( SDL_INIT_VIDEO );
    SDL_GPUDevice *d = SDL_CreateGPUDevice( SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr );
    if( !d ) {
        printf( "no device: %s\n", SDL_GetError() );
        return 1;
    }
    const char *dir = argv[1];
    char vp[512], fp[512];
    snprintf( vp, 512, "%s/r3d_block.vert.spv", dir );
    snprintf( fp, 512, "%s/r3d_block.frag.spv", dir );
    SDL_GPUShader *vs = load( d, vp, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1 ), *fs = load( d, fp,
                        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0 );
    if( !vs || !fs ) {
        return 1;
    }
    SDL_GPUVertexBufferDescription buffers[2] {};
    buffers[0] = { 0, sizeof( vertex ), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 };
    buffers[1] = { 1, 4, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 };
    SDL_GPUVertexAttribute attrs[3] {};
    attrs[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0 };
    attrs[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 12 };
    attrs[2] = { 2, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 0 };
    SDL_GPUColorTargetDescription ct{};
    ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.vertex_input_state = { buffers, 2, attrs, 3 };
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.target_info.color_target_descriptions = &ct;
    pci.target_info.num_color_targets = 1;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pci.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline *pipe = SDL_CreateGPUGraphicsPipeline( d, &pci );
    if( !pipe ) {
        printf( "pipeline: %s\n", SDL_GetError() );
        return 1;
    }
    const Uint32 W = 64, H = 64;
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    tci.width = W;
    tci.height = H;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    SDL_GPUTexture *color = SDL_CreateGPUTexture( d, &tci );
    tci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    tci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    SDL_GPUTexture *depth = SDL_CreateGPUTexture( d, &tci );
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = tci.height = 1;
    SDL_GPUTexture *white = SDL_CreateGPUTexture( d, &tci );
    SDL_GPUSamplerCreateInfo sci{};
    SDL_GPUSampler *sampler = SDL_CreateGPUSampler( d, &sci );
    // quad covering the screen at depth z (also its "height")
    const float z = 0.45f;
    vertex quad[4] = { { -1, -1, z, 0, 0 }, { 1, -1, z, 1, 0 }, { 1, 1, z, 1, 1 }, { -1, 1, z, 0, 1 } };
    Uint32 lights[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
    Uint32 idx[6] = { 0, 1, 2, 0, 2, 3 };
    Uint32 px = 0xffffffffu;
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( d );
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb );
    SDL_GPUBuffer *vb = upload( d, cp, SDL_GPU_BUFFERUSAGE_VERTEX, quad, sizeof quad ), *lb = upload( d,
                        cp, SDL_GPU_BUFFERUSAGE_VERTEX, lights, sizeof lights ), *ib = upload( d, cp,
                                SDL_GPU_BUFFERUSAGE_INDEX, idx, sizeof idx );
    {
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = 4;
        SDL_GPUTransferBuffer *t = SDL_CreateGPUTransferBuffer( d, &ti );
        memcpy( SDL_MapGPUTransferBuffer( d, t, false ), &px, 4 );
        SDL_UnmapGPUTransferBuffer( d, t );
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = t;
        src.pixels_per_row = 1;
        src.rows_per_layer = 1;
        SDL_GPUTextureRegion dst{};
        dst.texture = white;
        dst.w = dst.h = dst.d = 1;
        SDL_UploadToGPUTexture( cp, &src, &dst, false );
    }
    SDL_EndGPUCopyPass( cp );
    int failures = 0;
    for( int run = 0; run < 2; ++run ) {
        SDL_GPUColorTargetInfo cti{};
        cti.texture = color;
        cti.clear_color = { 0.f, 0.f, 1.f, 1.f };
        cti.load_op = SDL_GPU_LOADOP_CLEAR;
        cti.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo dti{};
        dti.texture = depth;
        dti.clear_depth = 1.f;
        dti.load_op = SDL_GPU_LOADOP_CLEAR;
        dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
        dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass( cb, &cti, 1, &dti );
        SDL_BindGPUGraphicsPipeline( rp, pipe );
        SDL_GPUBufferBinding vbs[2] = { { vb, 0 }, { lb, 0 } };
        SDL_BindGPUVertexBuffers( rp, 0, vbs, 2 );
        SDL_GPUBufferBinding ibb{ ib, 0 };
        SDL_BindGPUIndexBuffer( rp, &ibb, SDL_GPU_INDEXELEMENTSIZE_32BIT );
        SDL_GPUTextureSamplerBinding tsb{ white, sampler };
        SDL_BindGPUFragmentSamplers( rp, 0, &tsb, 1 );
        block_uniforms u{};
        for( int i = 0; i < 4; ++i ) {
            u.m[i * 5] = 1.f;    // identity
        }
        if( run == 0 ) {
            u.cut[0] = 0;
            u.cut[1] = 0;
            u.cut[2] = 0.5f;
            u.cut[3] = 0.3f;
            u.cut2[0] = 1.f;
            u.cut2[1] = 0.4f;
        }
        SDL_PushGPUVertexUniformData( cb, 0, &u, sizeof u );
        SDL_DrawGPUIndexedPrimitives( rp, 6, 1, 0, 0, 0 );
        SDL_EndGPURenderPass( rp );
        // read back
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        ti.size = W * H * 4;
        SDL_GPUTransferBuffer *t = SDL_CreateGPUTransferBuffer( d, &ti );
        SDL_GPUCopyPass *cp2 = SDL_BeginGPUCopyPass( cb );
        SDL_GPUTextureRegion src{};
        src.texture = color;
        src.w = W;
        src.h = H;
        src.d = 1;
        SDL_GPUTextureTransferInfo dst{};
        dst.transfer_buffer = t;
        dst.pixels_per_row = W;
        dst.rows_per_layer = H;
        SDL_DownloadFromGPUTexture( cp2, &src, &dst );
        SDL_EndGPUCopyPass( cp2 );
        SDL_GPUFence *f = SDL_SubmitGPUCommandBufferAndAcquireFence( cb );
        SDL_WaitForGPUFences( d, true, &f, 1 );
        SDL_ReleaseGPUFence( d, f );
        const Uint8 *m = ( const Uint8 * )SDL_MapGPUTransferBuffer( d, t, false );
        auto at = [ m ]( Uint32 x, Uint32 y ) {
            const Uint8 *p = m + ( y * W + x ) * 4;
            printf( "(%u,%u)=%d,%d,%d ", x, y, p[0], p[1], p[2] );
        };
        printf( "run %d (cut %s): ", run, run == 0 ? "on" : "off" );
        at( 32, 32 );
        at( 2, 2 );
        at( 32, 10 );
        printf( "\n" );
        const Uint8 *centre = m + ( 32 * W + 32 ) * 4;
        const Uint8 *corner = m + ( 2 * W + 2 ) * 4;
        const bool centre_clear = centre[0] == 0 && centre[2] == 255;
        const bool corner_quad = corner[0] == 255 && corner[1] == 255;
        if( !corner_quad || centre_clear != ( run == 0 ) ) {
            failures++;
        }
        SDL_UnmapGPUTransferBuffer( d, t );
        cb = SDL_AcquireGPUCommandBuffer( d );
    }
    printf( failures ? "FAILED\n" : "OK\n" );
    return failures ? 1 : 0;
}
