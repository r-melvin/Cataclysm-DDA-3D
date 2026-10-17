// M0 gate for the r3d renderer: proves that we can record our own SDL_GPU render
// pass (colour + depth) on the SDL_Renderer's GPU device, wrap the colour target
// as an SDL_Texture, and composite it with ordinary SDL_Render* calls.
//
// Build: g++ -std=c++17 -o spike spike.cpp -lSDL3
// Run:   spike <vert.spv> <frag.spv> <out.png>
//
// Sequence per frame: SDL_FlushRenderer -> own command buffer + pass -> submit ->
// SDL_RenderTexture of the wrapped target -> 2D overlay -> SDL_RenderReadPixels.

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <vector>

static std::vector<unsigned char> read_file( const char *path )
{
    std::vector<unsigned char> out;
    FILE *f = std::fopen( path, "rb" );
    if( !f ) {
        return out;
    }
    unsigned char buf[4096];
    size_t n = 0;
    while( ( n = std::fread( buf, 1, sizeof buf, f ) ) > 0 ) {
        out.insert( out.end(), buf, buf + n );
    }
    std::fclose( f );
    return out;
}

static SDL_GPUShader *make_shader( SDL_GPUDevice *dev, const char *path, SDL_GPUShaderStage stage,
                                   Uint32 uniform_buffers )
{
    const std::vector<unsigned char> code = read_file( path );
    if( code.empty() ) {
        std::fprintf( stderr, "cannot read %s\n", path );
        return nullptr;
    }
    SDL_GPUShaderCreateInfo info{};
    info.code_size = code.size();
    info.code = code.data();
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = stage;
    info.num_uniform_buffers = uniform_buffers;
    SDL_GPUShader *s = SDL_CreateGPUShader( dev, &info );
    if( !s ) {
        std::fprintf( stderr, "SDL_CreateGPUShader(%s): %s\n", path, SDL_GetError() );
    }
    return s;
}

#define CHECK(cond, what) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s: %s\n", what, SDL_GetError()); return 1; } } while (0)

int main( int argc, char **argv )
{
    if( argc < 4 ) {
        std::fprintf( stderr, "usage: spike vert.spv frag.spv out.png\n" );
        return 2;
    }
    CHECK( SDL_Init( SDL_INIT_VIDEO ), "SDL_Init" );
    const int W = 640;
    const int H = 360;
    SDL_Window *window = SDL_CreateWindow( "r3d spike", W, H, SDL_WINDOW_HIDDEN );
    CHECK( window, "SDL_CreateWindow" );

    // Same creation path as the game (sdl_wrappers.cpp create_game_renderer).
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty( props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window );
    SDL_SetStringProperty( props, SDL_PROP_RENDERER_CREATE_NAME_STRING, "gpu" );
    SDL_SetBooleanProperty( props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_SPIRV_BOOLEAN, true );
    SDL_Renderer *renderer = SDL_CreateRendererWithProperties( props );
    SDL_DestroyProperties( props );
    CHECK( renderer, "SDL_CreateRendererWithProperties" );
    std::printf( "renderer: %s\n", SDL_GetRendererName( renderer ) );

    SDL_GPUDevice *dev = SDL_GetGPURendererDevice( renderer );
    CHECK( dev, "SDL_GetGPURendererDevice" );
    std::printf( "gpu driver: %s\n", SDL_GetGPUDeviceDriver( dev ) );

    // Offscreen colour + depth targets.
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = 320;
    tci.height = 180;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    SDL_GPUTexture *color = SDL_CreateGPUTexture( dev, &tci );
    CHECK( color, "create colour target" );
    tci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    tci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    SDL_GPUTexture *depth = SDL_CreateGPUTexture( dev, &tci );
    CHECK( depth, "create depth target" );

    // Wrap the colour target for the 2D renderer.
    SDL_PropertiesID tp = SDL_CreateProperties();
    SDL_SetPointerProperty( tp, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, color );
    SDL_SetNumberProperty( tp, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32 );
    SDL_SetNumberProperty( tp, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET );
    SDL_SetNumberProperty( tp, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, 320 );
    SDL_SetNumberProperty( tp, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, 180 );
    SDL_Texture *wrapped = SDL_CreateTextureWithProperties( renderer, tp );
    SDL_DestroyProperties( tp );
    CHECK( wrapped, "wrap GPU texture as SDL_Texture" );
    SDL_SetTextureBlendMode( wrapped, SDL_BLENDMODE_NONE );
    SDL_SetTextureScaleMode( wrapped, SDL_SCALEMODE_NEAREST );

    SDL_GPUShader *vs = make_shader( dev, argv[1], SDL_GPU_SHADERSTAGE_VERTEX, 1 );
    SDL_GPUShader *fs = make_shader( dev, argv[2], SDL_GPU_SHADERSTAGE_FRAGMENT, 0 );
    if( !vs || !fs ) {
        return 1;
    }

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pci.depth_stencil_state.enable_depth_test = true;
    pci.depth_stencil_state.enable_depth_write = true;
    pci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.num_color_targets = 1;
    pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    pci.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline( dev, &pci );
    CHECK( pipeline, "create pipeline" );

    SDL_Surface *shot = nullptr;
    for( int frame = 0; frame < 3; ++frame ) {
        // Background colour from the 2D renderer first, so we also prove that
        // pending 2D work survives the flush.
        SDL_SetRenderDrawColor( renderer, 30, 30, 40, 255 );
        SDL_RenderClear( renderer );

        CHECK( SDL_FlushRenderer( renderer ), "SDL_FlushRenderer" );

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer( dev );
        CHECK( cmd, "SDL_AcquireGPUCommandBuffer" );
        SDL_GPUColorTargetInfo cti{};
        cti.texture = color;
        cti.clear_color = SDL_FColor{ 0.05f, 0.25f, 0.05f, 1.0f };
        cti.load_op = SDL_GPU_LOADOP_CLEAR;
        cti.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo dti{};
        dti.texture = depth;
        dti.clear_depth = 1.0f;
        dti.load_op = SDL_GPU_LOADOP_CLEAR;
        dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
        dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass( cmd, &cti, 1, &dti );
        SDL_BindGPUGraphicsPipeline( pass, pipeline );
        const float uniforms[4] = { 0.4f * frame, 320.0f / 180.0f, 0.0f, 0.0f };
        SDL_PushGPUVertexUniformData( cmd, 0, uniforms, sizeof uniforms );
        SDL_DrawGPUPrimitives( pass, 12, 1, 0, 0 );
        SDL_EndGPURenderPass( pass );
        CHECK( SDL_SubmitGPUCommandBuffer( cmd ), "SDL_SubmitGPUCommandBuffer" );

        const SDL_FRect dst{ 160.0f, 90.0f, 320.0f, 180.0f };
        CHECK( SDL_RenderTexture( renderer, wrapped, nullptr, &dst ), "SDL_RenderTexture" );

        // 2D overlay drawn after the 3D composite, like the curses sidebar.
        SDL_SetRenderDrawColor( renderer, 255, 255, 0, 255 );
        const SDL_FRect bar{ 400.0f, 0.0f, 240.0f, 40.0f };
        SDL_RenderFillRect( renderer, &bar );

        if( frame == 2 ) {
            shot = SDL_RenderReadPixels( renderer, nullptr );
            CHECK( shot, "SDL_RenderReadPixels" );
        } else {
            SDL_RenderPresent( renderer );
        }
    }
    CHECK( SDL_SavePNG( shot, argv[3] ), "SDL_SavePNG" );

    // Spot checks: centre of the near quad, spinning quad region, background, overlay.
    SDL_Surface *rgba = SDL_ConvertSurface( shot, SDL_PIXELFORMAT_RGBA32 );
    auto px = [&]( int x, int y, const char *what ) {
        const Uint8 *p = static_cast<const Uint8 *>( rgba->pixels ) + y * rgba->pitch + x * 4;
        std::printf( "%-28s (%3d,%3d) = %3d %3d %3d\n", what, x, y, p[0], p[1], p[2] );
    };
    px( 5, 5, "2D clear colour" );
    px( 5, 180, "left of 3D rect" );
    px( 170, 100, "3D clear (green)" );
    px( 330, 180, "near quad (blue family)" );
    px( 500, 20, "2D overlay (yellow)" );
    std::printf( "saved %s (%dx%d)\n", argv[3], shot->w, shot->h );

    SDL_DestroySurface( rgba );
    SDL_DestroySurface( shot );
    SDL_DestroyTexture( wrapped );
    SDL_ReleaseGPUGraphicsPipeline( dev, pipeline );
    SDL_ReleaseGPUShader( dev, vs );
    SDL_ReleaseGPUShader( dev, fs );
    SDL_ReleaseGPUTexture( dev, color );
    SDL_ReleaseGPUTexture( dev, depth );
    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();
    return 0;
}
