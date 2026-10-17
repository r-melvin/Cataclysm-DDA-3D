#pragma once
#ifndef CATA_SRC_R3D_GPU_H
#define CATA_SRC_R3D_GPU_H

#if defined(TILES)

#include <cstdint>
#include <memory>
#include <vector>

#include <SDL3/SDL.h>

#include "r3d_math.h"

namespace r3d
{

/** Per-vertex data of stream 0. Light colour lives in stream 1 as one RGBA8 value per vertex. */
struct vertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};
static_assert( sizeof( vertex ) == 20, "vertex layout is mirrored by the pipeline description" );

struct block_uniforms {
    mat4 view_proj;
    float offset[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    // xyz: world direction towards the camera; w: depth pull, in those units, of a vertex whose
    // light alpha is 1 (billboards; 0 for everything else). See r3d_block.vert.
    float pull[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    // Cutaway around the player (see r3d_block.frag): xy their world position, z the height
    // below which nothing is cut, w the half-width of the hole in tiles (0: off); cut2 xy the
    // view's forward direction on the ground, z the hole's half-length along it.
    float cut[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float cut2[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
};
static_assert( sizeof( block_uniforms ) == 128, "block_uniforms is mirrored by r3d_block.vert" );

/**
 * Owns the GPU objects of the 3D renderer: pipelines, sampler, and the offscreen colour+depth
 * target that gets composited into the SDL_Renderer scene. It shares the SDL_Renderer's own
 * SDL_GPUDevice (there is no second device), so every method must be called from the main
 * thread, and begin_frame() flushes the renderer's pending 2D work first.
 */
class gpu
{
    public:
        gpu() = default;
        gpu( const gpu & ) = delete;
        gpu &operator=( const gpu & ) = delete;
        ~gpu();

        /** Creates shaders, pipelines and the sampler. Fails (false) without a GPU renderer. */
        bool init( SDL_Renderer *renderer );
        /** Releases everything created by init and the target. Safe to call repeatedly. */
        void shutdown();
        bool ready() const {
            return device_ != nullptr && block_pipeline_ != nullptr;
        }
        SDL_Renderer *renderer() const {
            return renderer_;
        }

        /** (Re)creates the offscreen target when the size changes. */
        bool ensure_target( int width, int height );
        int target_width() const {
            return target_w_;
        }
        int target_height() const {
            return target_h_;
        }

        /**
         * A sprite sheet the 3D pass can sample, from an RGBA32 surface, uploaded on its own
         * command buffer (the renderer only submits its uploads when it presents). Wrapped as
         * an SDL_Texture of the renderer like the tileset sheets; empty on failure. The GPU
         * texture is released with it, so it must go before shutdown().
         */
        std::shared_ptr<SDL_Texture> create_sheet( SDL_Surface *rgba );
        SDL_GPUBuffer *create_buffer( SDL_GPUBufferUsageFlags usage, uint32_t size );
        void release_buffer( SDL_GPUBuffer *buffer );

        /** Starts a frame: flushes the renderer and acquires a command buffer. */
        bool begin_frame();
        /**
         * Queues a CPU->GPU copy; only valid between begin_frame and begin_pass. The data is
         * copied right away; all of a frame's uploads go through one staging buffer at begin_pass.
         */
        bool upload( SDL_GPUBuffer *dst, const void *data, uint32_t size, bool cycle = false );
        /**
         * Performs the queued uploads and opens the render pass (clearing colour and depth).
         * False means the frame is unusable and buffers uploaded this frame hold no valid data.
         */
        bool begin_pass( float r, float g, float b );
        /** Binds the block pipeline. */
        void bind_block_pipeline();
        /**
         * Draws indexed triangles from the given buffers with one sheet texture bound.
         * @param sheet an SDL_Texture of the GPU renderer holding the tileset sprites.
         */
        void draw_block( SDL_GPUBuffer *positions, SDL_GPUBuffer *lights, SDL_GPUBuffer *indices,
                         uint32_t first_index, uint32_t index_count, SDL_Texture *sheet,
                         const block_uniforms &uniforms );
        /** Ends the pass and submits. Returns the texture holding the result, or null on error. */
        SDL_Texture *end_frame();
        /** Abandons an unfinished frame (error path). */
        void abort_frame();

    private:
        SDL_GPUShader *load_shader( const char *basename, SDL_GPUShaderStage stage,
                                    uint32_t samplers, uint32_t uniform_buffers );
        void release_target();
        bool flush_uploads();
        void release_staging();

        SDL_Renderer *renderer_ = nullptr;
        SDL_GPUDevice *device_ = nullptr;
        SDL_GPUGraphicsPipeline *block_pipeline_ = nullptr;
        SDL_GPUSampler *sampler_ = nullptr;

        SDL_GPUTexture *color_ = nullptr;
        SDL_GPUTexture *depth_ = nullptr;
        SDL_Texture *wrapped_ = nullptr;
        SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        int target_w_ = 0;
        int target_h_ = 0;

        SDL_GPUCommandBuffer *cmd_ = nullptr;
        SDL_GPUCopyPass *copy_pass_ = nullptr;
        SDL_GPURenderPass *pass_ = nullptr;
        struct pending_upload {
            SDL_GPUBuffer *dst = nullptr;
            uint32_t offset = 0; // into pending_data_
            uint32_t size = 0;
            bool cycle = false;
        };
        std::vector<pending_upload> pending_;
        std::vector<uint8_t> pending_data_;
        // Reused every frame (mapped with cycling, so the previous frame's copy is not waited on).
        SDL_GPUTransferBuffer *staging_ = nullptr;
        uint32_t staging_size_ = 0;
};

} // namespace r3d

#endif // defined(TILES)

#endif // CATA_SRC_R3D_GPU_H
