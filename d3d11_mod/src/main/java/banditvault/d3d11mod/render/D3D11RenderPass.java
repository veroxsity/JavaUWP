package banditvault.d3d11mod.render;

import banditvault.d3d11mod.D3D11RenderSystem;
import banditvault.d3d11mod.D3D11ModInitializer;
import banditvault.d3d11mod.nativebridge.D3D11Native;
import com.mojang.blaze3d.buffers.GpuBuffer;
import com.mojang.blaze3d.buffers.GpuBufferSlice;
import com.mojang.blaze3d.pipeline.RenderPipeline;
import com.mojang.blaze3d.systems.RenderPass;
import com.mojang.blaze3d.systems.ScissorState;
import com.mojang.blaze3d.textures.FilterMode;
import com.mojang.blaze3d.textures.GpuSampler;
import com.mojang.blaze3d.textures.GpuTextureView;
import com.mojang.blaze3d.vertex.VertexFormat;
import com.mojang.blaze3d.vertex.VertexFormatElement;
import org.jetbrains.annotations.Nullable;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.function.Supplier;

final class D3D11RenderPass implements RenderPass {
    private static final int MAX_VERTEX_BUFFERS = 1;
    private static final String GUI_PIPELINE = "minecraft:pipeline/gui";
    private static final String GUI_TEXTURED_PIPELINE = "minecraft:pipeline/gui_textured";
    private static final String GUI_TEXT_PIPELINE = "minecraft:pipeline/gui_text";
    private static final String GUI_TEXT_HIGHLIGHT_PIPELINE = "minecraft:pipeline/gui_text_highlight";
    private static final String GUI_TEXTURED_PREMULTIPLIED_PIPELINE = "minecraft:pipeline/gui_textured_premultiplied_alpha";
    private static final String GUI_OPAQUE_TEXTURED_BACKGROUND_PIPELINE = "minecraft:pipeline/gui_opaque_textured_background";
    private static final String GUI_NAUSEA_OVERLAY_PIPELINE = "minecraft:pipeline/gui_nausea_overlay";
    private static final String MOJANG_LOGO_PIPELINE = "minecraft:pipeline/mojang_logo";
    private static final String CROSSHAIR_PIPELINE = "minecraft:pipeline/crosshair";
    private static final String VIGNETTE_PIPELINE = "minecraft:pipeline/vignette";
    private static final String ANIMATE_SPRITE_BLIT_PIPELINE = "minecraft:pipeline/animate_sprite_blit";
    private static final String ANIMATE_SPRITE_INTERPOLATE_PIPELINE = "minecraft:pipeline/animate_sprite_interpolate";
    private static final String LIGHTMAP_PIPELINE = "minecraft:pipeline/lightmap";
    private static final String SOLID_TERRAIN_PIPELINE = "minecraft:pipeline/solid_terrain";
    private static final String CUTOUT_TERRAIN_PIPELINE = "minecraft:pipeline/cutout_terrain";
    private static final String TRANSLUCENT_TERRAIN_PIPELINE = "minecraft:pipeline/translucent_terrain";
    private static final String SKY_PIPELINE = "minecraft:pipeline/sky";
    private static final String SUNRISE_SUNSET_PIPELINE = "minecraft:pipeline/sunrise_sunset";
    private static final String STARS_PIPELINE = "minecraft:pipeline/stars";
    private static final String CELESTIAL_PIPELINE = "minecraft:pipeline/celestial";

    // nativeDrawWorldSimple kinds/blends (see bandit_d3d11_backend.cpp).
    private static final int WORLD_KIND_SKY = 0;
    private static final int WORLD_KIND_POSITION_COLOR = 1;
    private static final int WORLD_KIND_STARS = 2;
    private static final int WORLD_KIND_POSITION_TEX = 3;
    private static final int WORLD_KIND_LINES = 4;
    private static final int WORLD_KIND_SHADOW = 5;
    private static final int WORLD_KIND_CRUMBLING = 6;
    private static final int WORLD_KIND_CLOUDS = 7;
    private static final int WORLD_KIND_PANORAMA = 8;
    private static final int WORLD_KIND_LEASH = 9;
    private static final int WORLD_KIND_GLINT = 10;
    private static final int WORLD_BLEND_NONE = 0;
    private static final int WORLD_BLEND_TRANSLUCENT = 1;
    private static final int WORLD_BLEND_OVERLAY = 2;
    private static final int WORLD_BLEND_CRUMBLING = 3;
    private static final int WORLD_BLEND_GLINT = 4;
    private static final String LINES_PIPELINE = "minecraft:pipeline/lines";
    private static final String ENTITY_SHADOW_PIPELINE = "minecraft:pipeline/entity_shadow";
    private static final String CRUMBLING_PIPELINE = "minecraft:pipeline/crumbling";
    private static final String CLOUDS_PIPELINE = "minecraft:pipeline/clouds";
    private static final String PANORAMA_PIPELINE = "minecraft:pipeline/panorama";
    private static final String LEASH_PIPELINE = "minecraft:pipeline/leash";
    private static final String GLINT_PIPELINE = "minecraft:pipeline/glint";
    private static final String TEXT_PIPELINE = "minecraft:pipeline/text";
    private static final String TEXT_POLYGON_OFFSET_PIPELINE = "minecraft:pipeline/text_polygon_offset";
    private static final String TEXT_SEE_THROUGH_PIPELINE = "minecraft:pipeline/text_see_through";

    // Entity flag bits, mirrored in bandit_d3d11_backend.cpp.
    private static final int ENTITY_FLAG_PER_FACE_LIGHTING = 1;
    private static final int ENTITY_FLAG_NO_OVERLAY = 2;
    private static final int ENTITY_FLAG_EMISSIVE = 4;
    private static final int ENTITY_FLAG_NO_CARDINAL_LIGHTING = 8;
    private static final int ENTITY_FLAG_CULL_BACK = 16;
    private static final int ENTITY_FLAG_NO_DEPTH_WRITE = 64;

    private record EntityPipeline(int kind, int blend, float cutoff, int flags) {
    }

    // Flags derived from the RenderPipelines bytecode (ALPHA_CUTOUT / PER_FACE_LIGHTING /
    // blend / cull per pipeline). kind 0 = NEW_ENTITY format, kind 1 = PARTICLE format.
    private static final HashMap<String, EntityPipeline> ENTITY_PIPELINES = new HashMap<>();
    static {
        ENTITY_PIPELINES.put("minecraft:pipeline/entity_solid", new EntityPipeline(0, 0, 0.0f, ENTITY_FLAG_CULL_BACK));
        // Item frames; identical state to entity_solid (the z-offset is applied upstream).
        ENTITY_PIPELINES.put("minecraft:pipeline/entity_solid_offset_forward", new EntityPipeline(0, 0, 0.0f, ENTITY_FLAG_CULL_BACK));
        ENTITY_PIPELINES.put("minecraft:pipeline/solid_block", new EntityPipeline(0, 0, 0.0f, ENTITY_FLAG_CULL_BACK));
        ENTITY_PIPELINES.put("minecraft:pipeline/cutout_block", new EntityPipeline(0, 0, 0.5f, ENTITY_FLAG_CULL_BACK));
        ENTITY_PIPELINES.put("minecraft:pipeline/entity_cutout", new EntityPipeline(0, 0, 0.1f, ENTITY_FLAG_CULL_BACK));
        ENTITY_PIPELINES.put("minecraft:pipeline/entity_cutout_no_cull", new EntityPipeline(0, 0, 0.1f, ENTITY_FLAG_PER_FACE_LIGHTING));
        ENTITY_PIPELINES.put("minecraft:pipeline/entity_cutout_no_cull_z_offset", new EntityPipeline(0, 0, 0.1f, ENTITY_FLAG_PER_FACE_LIGHTING));
        ENTITY_PIPELINES.put("minecraft:pipeline/entity_smooth_cutout", new EntityPipeline(0, 0, 0.1f, ENTITY_FLAG_CULL_BACK));
        ENTITY_PIPELINES.put("minecraft:pipeline/entity_translucent", new EntityPipeline(0, 1, 0.1f, ENTITY_FLAG_PER_FACE_LIGHTING));
        ENTITY_PIPELINES.put("minecraft:pipeline/entity_translucent_emissive", new EntityPipeline(0, 1, 0.1f, ENTITY_FLAG_PER_FACE_LIGHTING | ENTITY_FLAG_EMISSIVE));
        ENTITY_PIPELINES.put("minecraft:pipeline/item_entity_translucent_cull", new EntityPipeline(0, 1, 0.1f, ENTITY_FLAG_CULL_BACK));
        ENTITY_PIPELINES.put("minecraft:pipeline/translucent_moving_block", new EntityPipeline(0, 1, 0.0f, 0));
        ENTITY_PIPELINES.put("minecraft:pipeline/opaque_particle", new EntityPipeline(1, 0, 0.0f, 0));
        ENTITY_PIPELINES.put("minecraft:pipeline/translucent_particle", new EntityPipeline(1, 1, 0.0f, 0));
        ENTITY_PIPELINES.put("minecraft:pipeline/weather_depth_write", new EntityPipeline(1, 1, 0.0f, 0));
        ENTITY_PIPELINES.put("minecraft:pipeline/weather_no_depth_write", new EntityPipeline(1, 1, 0.0f, ENTITY_FLAG_NO_DEPTH_WRITE));
        ENTITY_PIPELINES.put("minecraft:pipeline/armor_cutout_no_cull", new EntityPipeline(0, 0, 0.1f, ENTITY_FLAG_PER_FACE_LIGHTING | ENTITY_FLAG_NO_OVERLAY));
        ENTITY_PIPELINES.put("minecraft:pipeline/armor_decal_cutout_no_cull", new EntityPipeline(0, 0, 0.1f, ENTITY_FLAG_PER_FACE_LIGHTING | ENTITY_FLAG_NO_OVERLAY));
        ENTITY_PIPELINES.put("minecraft:pipeline/armor_translucent", new EntityPipeline(0, 1, 0.1f, ENTITY_FLAG_PER_FACE_LIGHTING | ENTITY_FLAG_NO_OVERLAY));
        ENTITY_PIPELINES.put("minecraft:pipeline/eyes", new EntityPipeline(0, 1, 0.1f, ENTITY_FLAG_PER_FACE_LIGHTING | ENTITY_FLAG_EMISSIVE));
    }

    // Pixel shader semantics, mirroring the vanilla core shader each pipeline references.
    private static final int PS_MODE_COLOR = 0;      // core/gui.fsh: discard a==0, then * ColorModulator
    private static final int PS_MODE_TEXTURED = 1;   // core/position_tex_color.fsh: tex*color, discard a==0, * ColorModulator
    private static final int PS_MODE_LINEAR_SAMPLER = 0x100; // bound Sampler0 mag filter is LINEAR
    private static final int PS_MODE_TEXT = 2;       // core/rendertype_text.fsh: tex*color*ColorModulator, discard a<0.1

    // Blend functions used by the whitelisted GUI pipelines (from RenderPipelines bytecode).
    private static final int BLEND_TRANSLUCENT = 0;  // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
    private static final int BLEND_ADDITIVE_ALPHA = 1; // SRC_ALPHA, ONE (mojang_logo, gui_nausea_overlay)
    private static final int BLEND_PREMULTIPLIED = 2;  // ONE, ONE_MINUS_SRC_ALPHA
    private static final int BLEND_NONE = 3;           // blending disabled
    private static final int BLEND_INVERT = 4;         // ONE_MINUS_DST_COLOR, ONE_MINUS_SRC_COLOR (crosshair)
    private static final int BLEND_MULTIPLY_DARKEN = 5; // ZERO, ONE_MINUS_SRC_COLOR (vignette)

    private static final int MAX_LOGGED_DRAWS_PER_PIPELINE = 4;

    // ponytail: world rendering is not implemented yet; unknown pipelines are skipped (logged
    // once per pipeline+operation) instead of crashing, so gameplay can be tested while the
    // skip log doubles as the VulkanMod-parity worklist.
    private static final HashSet<String> loggedSkippedPipelines = new HashSet<>();
    private static final HashMap<String, Integer> loggedDrawsPerPipeline = new HashMap<>();
    private static boolean loggedFirstGuiDrawInputs;
    private static boolean loggedFirstTexturedGuiDrawInputs;

    private final boolean hasDepthTexture;
    private final boolean backbufferTarget;
    private final long targetTexture;
    private final int targetMip;
    private final GpuBuffer[] vertexBuffers = new GpuBuffer[MAX_VERTEX_BUFFERS];
    private final HashMap<String, GpuBufferSlice> uniforms = new HashMap<>();
    private final HashMap<String, TextureAndSampler> samplers = new HashMap<>();
    private final ScissorState scissorState = new ScissorState();
    private @Nullable RenderPipeline pipeline;
    private @Nullable GpuBuffer indexBuffer;
    private VertexFormat.IndexType indexType = VertexFormat.IndexType.INT;
    private int pushedDebugGroups;
    private boolean closed;

    D3D11RenderPass(boolean hasDepthTexture, boolean backbufferTarget, long targetTexture, int targetMip) {
        this.hasDepthTexture = hasDepthTexture;
        this.backbufferTarget = backbufferTarget;
        this.targetTexture = targetTexture;
        this.targetMip = targetMip;
    }

    boolean hasDepthTexture() {
        return this.hasDepthTexture;
    }

    @Override
    public void pushDebugGroup(Supplier<String> supplier) {
        checkOpen();
        this.pushedDebugGroups++;
    }

    @Override
    public void popDebugGroup() {
        checkOpen();
        if (this.pushedDebugGroups == 0) {
            throw new IllegalStateException("Can't pop more debug groups than were pushed");
        }
        this.pushedDebugGroups--;
    }

    @Override
    public void setPipeline(RenderPipeline pipeline) {
        checkOpen();
        this.pipeline = pipeline;
    }

    @Override
    public void bindTexture(String name, @Nullable GpuTextureView view, @Nullable GpuSampler sampler) {
        checkOpen();
        if (view == null || sampler == null) {
            this.samplers.remove(name);
            return;
        }
        this.samplers.put(name, new TextureAndSampler(view, sampler));
    }

    @Override
    public void setUniform(String name, GpuBuffer buffer) {
        this.setUniform(name, buffer.slice());
    }

    @Override
    public void setUniform(String name, GpuBufferSlice slice) {
        checkOpen();
        if (slice.offset() % D3D11GpuDevice.UNIFORM_OFFSET_ALIGNMENT != 0) {
            throw new IllegalArgumentException("Uniform buffer offset must be aligned to " + D3D11GpuDevice.UNIFORM_OFFSET_ALIGNMENT);
        }
        this.uniforms.put(name, slice);
    }

    @Override
    public void enableScissor(int x, int y, int width, int height) {
        checkOpen();
        this.scissorState.enable(x, y, width, height);
    }

    @Override
    public void disableScissor() {
        checkOpen();
        this.scissorState.disable();
    }

    @Override
    public void setVertexBuffer(int slot, GpuBuffer buffer) {
        checkOpen();
        if (slot < 0 || slot >= MAX_VERTEX_BUFFERS) {
            throw new IllegalArgumentException("Vertex buffer slot is out of range: " + slot);
        }
        this.vertexBuffers[slot] = buffer;
    }

    @Override
    public void setIndexBuffer(@Nullable GpuBuffer buffer, VertexFormat.IndexType indexType) {
        checkOpen();
        this.indexBuffer = buffer;
        this.indexType = indexType;
    }

    @Override
    public void drawIndexed(int vertexOffset, int firstIndex, int vertexCount, int instanceCount) {
        checkOpen();
        String indexedLocation = pipelineLocation();
        EntityPipeline entityPipeline = ENTITY_PIPELINES.get(indexedLocation);
        if (entityPipeline != null) {
            if (!this.backbufferTarget && this.targetTexture == 0L) {
                logSkippedPipeline("drawIndexed(entity target without native texture)");
                return;
            }
            if (!(this.vertexBuffers[0] instanceof D3D11GpuBuffer entityVertexBuffer)) {
                throw unsupported("drawIndexed(missing/foreign vertex buffer)");
            }
            if (!(this.indexBuffer instanceof D3D11GpuBuffer entityIndexBuffer)) {
                throw unsupported("drawIndexed(missing/foreign index buffer)");
            }
            ByteBuffer dyn = uniformData("DynamicTransforms");
            ByteBuffer proj = uniformData("Projection");
            ByteBuffer fogBuffer = optionalUniformData("Fog");
            ByteBuffer lighting = entityPipeline.kind() == 0 ? optionalUniformData("Lighting") : null;
            long texture = boundTextureHandle();
            long overlay = optionalSamplerTextureHandle("Sampler1");
            long lightmap = optionalSamplerTextureHandle("Sampler2");
            int flags = entityPipeline.flags();
            if (overlay == 0L) {
                flags |= ENTITY_FLAG_NO_OVERLAY;
            }
            if (lightmap == 0L) {
                flags |= ENTITY_FLAG_EMISSIVE;
            }
            if (lighting == null && entityPipeline.kind() == 0) {
                flags |= ENTITY_FLAG_NO_CARDINAL_LIGHTING;
            }
            boolean ok = D3D11Native.drawEntity(
                entityPipeline.kind(),
                entityPipeline.blend(),
                entityPipeline.cutoff(),
                flags,
                entityVertexBuffer.nativeHandle(),
                this.pipeline.getVertexFormat().getVertexSize(),
                entityIndexBuffer.nativeHandle(),
                this.indexType.bytes,
                vertexOffset,
                firstIndex,
                vertexCount,
                texture,
                overlay,
                lightmap,
                this.backbufferTarget ? 0L : this.targetTexture,
                this.targetMip,
                this.hasDepthTexture,
                dyn,
                proj,
                fogBuffer,
                lighting
            );
            if (!ok) {
                throw unsupported("drawIndexed(native entity failed)");
            }
            return;
        }
        boolean textNormal = TEXT_PIPELINE.equals(indexedLocation);
        boolean textOffset = TEXT_POLYGON_OFFSET_PIPELINE.equals(indexedLocation);
        boolean textSeeThrough = TEXT_SEE_THROUGH_PIPELINE.equals(indexedLocation);
        if (textNormal || textOffset || textSeeThrough) {
            // In-world text: signs (polygon offset), nametags (see-through), floating text.
            if (!this.backbufferTarget) {
                logSkippedPipeline("drawIndexed(text texture target)");
                return;
            }
            if (!(this.vertexBuffers[0] instanceof D3D11GpuBuffer textVb) || !(this.indexBuffer instanceof D3D11GpuBuffer textIb)) {
                throw unsupported("drawIndexed(missing/foreign text buffers)");
            }
            boolean textOk = D3D11Native.drawText(
                textVb.nativeHandle(),
                this.pipeline.getVertexFormat().getVertexSize(),
                textIb.nativeHandle(),
                this.indexType.bytes,
                vertexOffset,
                firstIndex,
                vertexCount,
                samplerTextureHandle("Sampler0"),
                samplerTextureHandle("Sampler2"),
                textOffset ? 1 : (textSeeThrough ? 2 : 0),
                uniformData("DynamicTransforms"),
                uniformData("Projection"),
                optionalUniformData("Fog"));
            if (!textOk) {
                throw unsupported("drawIndexed(native text failed)");
            }
            return;
        }
        if (LINES_PIPELINE.equals(indexedLocation) || ENTITY_SHADOW_PIPELINE.equals(indexedLocation) || CRUMBLING_PIPELINE.equals(indexedLocation) || CLOUDS_PIPELINE.equals(indexedLocation) || PANORAMA_PIPELINE.equals(indexedLocation) || LEASH_PIPELINE.equals(indexedLocation) || GLINT_PIPELINE.equals(indexedLocation)) {
            if (!this.backbufferTarget) {
                if (GLINT_PIPELINE.equals(indexedLocation) && this.targetTexture != 0L) {
                    // Vanilla re-bakes glinted icons continuously; skipping these kills hotbar shimmer.
                    if (!(this.vertexBuffers[0] instanceof D3D11GpuBuffer glintVb) || !(this.indexBuffer instanceof D3D11GpuBuffer glintIb)) {
                        throw unsupported("drawIndexed(missing/foreign glint buffers)");
                    }
                    boolean bakeOk = D3D11Native.drawGlintToTexture(
                        glintVb.nativeHandle(),
                        this.pipeline.getVertexFormat().getVertexSize(),
                        glintIb.nativeHandle(),
                        this.indexType.bytes,
                        vertexOffset,
                        firstIndex,
                        vertexCount,
                        boundTextureHandle(),
                        this.targetTexture,
                        this.targetMip,
                        this.hasDepthTexture,
                        uniformData("DynamicTransforms"),
                        uniformData("Projection"),
                        optionalUniformData("Fog"),
                        optionalUniformData("Globals"));
                    if (!bakeOk) {
                        throw unsupported("drawIndexed(native glint bake failed)");
                    }
                    return;
                }
                logSkippedPipeline("drawIndexed(world texture target)");
                return;
            }
            boolean clouds = CLOUDS_PIPELINE.equals(indexedLocation);
            boolean lines = LINES_PIPELINE.equals(indexedLocation);
            boolean crumbling = CRUMBLING_PIPELINE.equals(indexedLocation);
            boolean panorama = PANORAMA_PIPELINE.equals(indexedLocation);
            boolean leash = LEASH_PIPELINE.equals(indexedLocation);
            boolean glint = GLINT_PIPELINE.equals(indexedLocation);
            D3D11GpuBuffer worldVb = this.vertexBuffers[0] instanceof D3D11GpuBuffer buffer ? buffer : null;
            if (!clouds && worldVb == null) {
                throw unsupported("drawIndexed(missing/foreign vertex buffer)");
            }
            if (!(this.indexBuffer instanceof D3D11GpuBuffer worldIb)) {
                throw unsupported("drawIndexed(missing/foreign index buffer)");
            }
            ByteBuffer dyn = uniformData("DynamicTransforms");
            ByteBuffer proj = uniformData("Projection");
            ByteBuffer fogBuffer = optionalUniformData("Fog");
            ByteBuffer globals = lines ? uniformData("Globals") : optionalUniformData("Globals");
            ByteBuffer cloudInfo = clouds ? uniformData("CloudInfo") : null;
            long cloudFacesHandle = 0L;
            int cloudFacesOffset = 0;
            int cloudFacesLength = 0;
            if (clouds) {
                GpuBufferSlice facesSlice = this.uniforms.get("CloudFaces");
                if (facesSlice == null || !(facesSlice.buffer() instanceof D3D11GpuBuffer facesBuffer)) {
                    throw unsupported("drawIndexed(missing CloudFaces buffer)");
                }
                cloudFacesHandle = facesBuffer.nativeHandle();
                cloudFacesOffset = Math.toIntExact(facesSlice.offset());
                cloudFacesLength = Math.toIntExact(facesSlice.length());
            }
            long texture = leash ? samplerTextureHandle("Sampler2") : ((ENTITY_SHADOW_PIPELINE.equals(indexedLocation) || crumbling || panorama || glint) ? boundTextureHandle() : 0L);
            int kind = glint ? WORLD_KIND_GLINT : (leash ? WORLD_KIND_LEASH : (panorama ? WORLD_KIND_PANORAMA : (clouds ? WORLD_KIND_CLOUDS : (lines ? WORLD_KIND_LINES : (crumbling ? WORLD_KIND_CRUMBLING : WORLD_KIND_SHADOW)))));
            int blend = glint ? WORLD_BLEND_GLINT : ((panorama || leash) ? WORLD_BLEND_NONE : (crumbling ? WORLD_BLEND_CRUMBLING : WORLD_BLEND_TRANSLUCENT));
            boolean ok = D3D11Native.drawWorldSimple(
                kind,
                blend,
                false,
                worldVb != null ? worldVb.nativeHandle() : 0L,
                this.pipeline.getVertexFormat().getVertexSize(),
                worldIb.nativeHandle(),
                this.indexType.bytes,
                vertexOffset,
                firstIndex,
                vertexCount,
                texture,
                dyn,
                proj,
                fogBuffer,
                globals,
                cloudInfo,
                cloudFacesHandle,
                cloudFacesOffset,
                cloudFacesLength
            );
            if (!ok) {
                throw unsupported("drawIndexed(native world simple failed)");
            }
            return;
        }
        if (STARS_PIPELINE.equals(indexedLocation) || CELESTIAL_PIPELINE.equals(indexedLocation)) {
            // Night sky quads (OVERLAY blend, no depth): stars are untextured ColorModulator
            // fills, celestial samples the celestials atlas.
            if (!this.backbufferTarget) {
                logSkippedPipeline("drawIndexed(world texture target)");
                return;
            }
            boolean celestial = CELESTIAL_PIPELINE.equals(indexedLocation);
            if (!(this.vertexBuffers[0] instanceof D3D11GpuBuffer worldVertexBuffer)) {
                throw unsupported("drawIndexed(missing/foreign vertex buffer)");
            }
            if (!(this.indexBuffer instanceof D3D11GpuBuffer worldIndexBuffer)) {
                throw unsupported("drawIndexed(missing/foreign index buffer)");
            }
            ByteBuffer dyn = uniformData("DynamicTransforms");
            ByteBuffer proj = uniformData("Projection");
            long texture = celestial ? boundTextureHandle() : 0L;
            boolean ok = D3D11Native.drawWorldSimple(
                celestial ? WORLD_KIND_POSITION_TEX : WORLD_KIND_STARS,
                WORLD_BLEND_OVERLAY,
                false,
                worldVertexBuffer.nativeHandle(),
                this.pipeline.getVertexFormat().getVertexSize(),
                worldIndexBuffer.nativeHandle(),
                this.indexType.bytes,
                vertexOffset,
                firstIndex,
                vertexCount,
                texture,
                dyn,
                proj,
                null,
                null,
                null,
                0L,
                0,
                0
            );
            if (!ok) {
                throw unsupported("drawIndexed(native world simple failed)");
            }
            return;
        }
        if (!isSupportedGuiPipeline()) {
            logSkippedPipeline("drawIndexed");
            return;
        }
        if (!this.backbufferTarget) {
            // ponytail: GUI-style draws into texture render targets (maps, books) need target
            // plumbing on the gui path; skip until something visible depends on it.
            logSkippedPipeline("drawIndexed(texture target)");
            return;
        }
        if (!(this.vertexBuffers[0] instanceof D3D11GpuBuffer vertexBuffer)) {
            throw unsupported("drawIndexed(missing/foreign vertex buffer)");
        }
        if (!(this.indexBuffer instanceof D3D11GpuBuffer indexBuffer)) {
            throw unsupported("drawIndexed(missing/foreign index buffer)");
        }
        ByteBuffer dynamicTransforms = uniformData("DynamicTransforms");
        ByteBuffer projection = uniformData("Projection");

        // Vertex layout comes straight from the pipeline's vertex format (VulkanMod-style)
        // instead of hardcoded per-pipeline offsets:
        //   gui / gui_text_highlight  = POSITION_COLOR            (color@12, stride 16)
        //   gui_textured, mojang_logo = POSITION_TEX_COLOR        (uv@12, color@20, stride 24)
        //   gui_text                  = POSITION_COLOR_TEX_LIGHTMAP (color@12, uv@16, stride 28; lightmap ignored)
        VertexFormat format = this.pipeline.getVertexFormat();
        int stride = format.getVertexSize();
        int uvOffset = format.contains(VertexFormatElement.UV0) ? format.getOffset(VertexFormatElement.UV0) : -1;
        int colorOffset = format.contains(VertexFormatElement.COLOR) ? format.getOffset(VertexFormatElement.COLOR) : -1;
        if (colorOffset < 0) {
            throw unsupported("drawIndexed(vertex format without COLOR: " + format + ")");
        }
        long textureHandle = uvOffset >= 0 ? boundTextureHandle() : 0L;
        int psMode = uvOffset < 0 ? PS_MODE_COLOR : (isGuiTextPipeline() ? PS_MODE_TEXT : PS_MODE_TEXTURED);
        if (uvOffset >= 0 && boundSamplerIsLinear()) {
            psMode |= PS_MODE_LINEAR_SAMPLER;
        }
        int blendMode = blendMode();
        boolean scissor = this.scissorState.enabled();
        int scissorX = scissor ? this.scissorState.x() : 0;
        int scissorY = scissor ? this.scissorState.y() : 0;
        int scissorWidth = scissor ? this.scissorState.width() : -1;
        int scissorHeight = scissor ? this.scissorState.height() : -1;
        // The game's own index buffer is used directly (it is correct now that mapBuffer keeps
        // native byte order). firstIndex slices batched GUI meshes, so it must pass through
        // unchanged — an earlier substitute index buffer zeroed it, which redrew the first
        // quads of every batch and made atlas sprites (buttons/toasts/hotbar) invisible.
        logFirstGuiInputs(vertexBuffer, indexBuffer, dynamicTransforms, projection, vertexOffset, firstIndex, vertexCount, this.indexType, stride, uvOffset, colorOffset, textureHandle != 0L);
        int drawsLogged = loggedDrawsPerPipeline.merge(pipelineLocation(), 1, Integer::sum);
        if (drawsLogged <= MAX_LOGGED_DRAWS_PER_PIPELINE) {
            D3D11ModInitializer.LOGGER.warn("D3D11 gui draw pipeline={} n={} tex={} stride={} indexType={} indexCount={} vertexOffset={} firstIndex={} blend={} scissor={}",
                this.pipeline.getLocation(), drawsLogged, textureHandle, stride, this.indexType, vertexCount, vertexOffset, firstIndex, blendMode,
                scissor ? scissorX + "," + scissorY + "," + scissorWidth + "," + scissorHeight : "off");
        }
        boolean ok = D3D11Native.drawGuiIndexed(
            vertexBuffer.nativeHandle(),
            stride,
            indexBuffer.nativeHandle(),
            this.indexType.bytes,
            vertexOffset,
            firstIndex,
            vertexCount,
            instanceCount,
            uvOffset,
            colorOffset,
            psMode,
            blendMode,
            scissorX,
            scissorY,
            scissorWidth,
            scissorHeight,
            textureHandle,
            dynamicTransforms,
            projection
        );
        if (!ok) {
            throw unsupported("drawIndexed(native gui failed)");
        }
    }

    @Override
    public <T> void drawMultipleIndexed(Collection<RenderPass.Draw<T>> draws, @Nullable GpuBuffer indexBuffer, @Nullable VertexFormat.IndexType indexType, Collection<String> dynamicUniforms, T userObject) {
        checkOpen();
        String location = pipelineLocation();
        boolean solid = SOLID_TERRAIN_PIPELINE.equals(location);
        boolean cutout = CUTOUT_TERRAIN_PIPELINE.equals(location);
        boolean translucent = TRANSLUCENT_TERRAIN_PIPELINE.equals(location);
        if (!solid && !cutout && !translucent) {
            logSkippedPipeline("drawMultipleIndexed");
            return;
        }
        if (!this.backbufferTarget) {
            logSkippedPipeline("drawMultipleIndexed(texture target)");
            return;
        }
        ByteBuffer projection = uniformData("Projection");
        ByteBuffer fog = uniformData("Fog");
        ByteBuffer globals = uniformData("Globals");
        long atlasTexture = boundTextureHandle();
        long lightmapTexture = samplerTextureHandle("Sampler2");
        // ALPHA_CUTOUT shader defines from RenderPipelines: cutout_terrain=0.5, translucent_terrain=0.01.
        float alphaCutoff = cutout ? 0.5f : (translucent ? 0.01f : 0.0f);
        int stride = this.pipeline.getVertexFormat().getVertexSize();
        // Batch API: shared state + uniforms bound once, per-section draws stay minimal
        // (one ChunkSection upload + buffer binds + DrawIndexed per section).
        if (!D3D11Native.beginTerrainBatch(alphaCutoff, translucent, atlasTexture, lightmapTexture, projection, fog, globals)) {
            throw unsupported("drawMultipleIndexed(native terrain batch failed)");
        }
        try {
            HashMap<String, GpuBufferSlice> perDrawUniforms = new HashMap<>();
            for (RenderPass.Draw<T> draw : draws) {
                if (!(draw.vertexBuffer() instanceof D3D11GpuBuffer vertexBuffer)) {
                    throw unsupported("drawMultipleIndexed(missing/foreign vertex buffer)");
                }
                GpuBuffer drawIndexBufferRaw = draw.indexBuffer() != null ? draw.indexBuffer() : indexBuffer;
                VertexFormat.IndexType drawIndexType = draw.indexType() != null ? draw.indexType() : indexType;
                if (!(drawIndexBufferRaw instanceof D3D11GpuBuffer drawIndexBuffer) || drawIndexType == null) {
                    throw unsupported("drawMultipleIndexed(missing/foreign index buffer)");
                }
                perDrawUniforms.clear();
                if (draw.uniformUploaderConsumer() != null) {
                    draw.uniformUploaderConsumer().accept(userObject, perDrawUniforms::put);
                }
                GpuBufferSlice chunkSlice = perDrawUniforms.get("ChunkSection");
                ByteBuffer chunkSection = chunkSlice != null ? sliceData(chunkSlice, "ChunkSection") : uniformData("ChunkSection");
                boolean ok = D3D11Native.drawTerrainSection(
                    vertexBuffer.nativeHandle(),
                    stride,
                    drawIndexBuffer.nativeHandle(),
                    drawIndexType.bytes,
                    draw.firstIndex(),
                    draw.indexCount(),
                    chunkSection
                );
                if (!ok) {
                    throw unsupported("drawMultipleIndexed(native terrain failed)");
                }
            }
        } finally {
            D3D11Native.endTerrainBatch();
        }
    }

    @Override
    public void draw(int vertexOffset, int vertexCount) {
        checkOpen();
        String location = pipelineLocation();
        if (LIGHTMAP_PIPELINE.equals(location)) {
            // Fullscreen triangle into the 16x16 lightmap texture (core/screenquad.vsh +
            // core/lightmap.fsh). The result feeds Sampler2 of every world shader.
            if (this.targetTexture == 0L) {
                logSkippedPipeline("draw(lightmap without texture target)");
                return;
            }
            ByteBuffer lightmapInfo = uniformData("LightmapInfo");
            if (!D3D11Native.drawLightmap(this.targetTexture, this.targetMip, vertexOffset, vertexCount, lightmapInfo)) {
                throw unsupported("draw(native lightmap failed)");
            }
            return;
        }
        if (SKY_PIPELINE.equals(location) || SUNRISE_SUNSET_PIPELINE.equals(location)) {
            // Sky disc / sunrise band: TRIANGLE_FAN draws converted to an indexed triangle list
            // natively. Rendered before terrain with no depth so the world draws over it.
            if (!this.backbufferTarget) {
                logSkippedPipeline("draw(world texture target)");
                return;
            }
            boolean sunrise = SUNRISE_SUNSET_PIPELINE.equals(location);
            if (!(this.vertexBuffers[0] instanceof D3D11GpuBuffer skyVertexBuffer)) {
                throw unsupported("draw(missing/foreign vertex buffer)");
            }
            ByteBuffer dyn = uniformData("DynamicTransforms");
            ByteBuffer proj = uniformData("Projection");
            ByteBuffer fogBuffer = sunrise ? null : uniformData("Fog");
            boolean ok = D3D11Native.drawWorldSimple(
                sunrise ? WORLD_KIND_POSITION_COLOR : WORLD_KIND_SKY,
                sunrise ? WORLD_BLEND_TRANSLUCENT : WORLD_BLEND_NONE,
                true,
                skyVertexBuffer.nativeHandle(),
                this.pipeline.getVertexFormat().getVertexSize(),
                0L,
                0,
                vertexOffset,
                0,
                vertexCount,
                0L,
                dyn,
                proj,
                fogBuffer,
                null,
                null,
                0L,
                0,
                0
            );
            if (!ok) {
                throw unsupported("draw(native world simple failed)");
            }
            return;
        }
        boolean blit = ANIMATE_SPRITE_BLIT_PIPELINE.equals(location);
        boolean interpolate = ANIMATE_SPRITE_INTERPOLATE_PIPELINE.equals(location);
        if (blit || interpolate) {
            // Atlas population/animation: vertex-buffer-less quad into the atlas render target
            // (see core/animate_sprite.vsh). Without this every atlas stays empty, which is why
            // buttons/toasts/hotbar/crosshair sprites were invisible.
            if (this.targetTexture == 0L) {
                logSkippedPipeline("draw(sprite blit without texture target)");
                return;
            }
            ByteBuffer spriteInfo = uniformData("SpriteAnimationInfo");
            long sprite = samplerTextureHandle(interpolate ? "CurrentSprite" : "Sprite");
            long nextSprite = interpolate ? samplerTextureHandle("NextSprite") : 0L;
            boolean ok = D3D11Native.drawSpriteBlit(this.targetTexture, this.targetMip, interpolate, sprite, nextSprite, vertexOffset, vertexCount, spriteInfo);
            if (!ok) {
                throw unsupported("draw(native sprite blit failed)");
            }
            return;
        }
        if (location.startsWith("minecraft:blur/")) {
            // Menu blur: "main" is the backbuffer here - input marker 0 = native snapshot.
            TextureAndSampler in = this.samplers.get("InSampler");
            if (in == null && !this.samplers.isEmpty()) {
                in = this.samplers.values().iterator().next();
            }
            long inputHandle = 0L;
            if (in != null && in.view().texture() instanceof D3D11GpuTexture inTexture && inTexture.hasNativeHandle()) {
                String inLabel = in.view().texture().getLabel();
                if (inLabel == null || !inLabel.startsWith("Main")) {
                    inputHandle = inTexture.nativeHandle();
                }
            }
            ByteBuffer blurConfig = uniformData("BlurConfig");
            ByteBuffer postGlobals = optionalUniformData("Globals");
            boolean ok = D3D11Native.drawPostPass(
                inputHandle,
                this.backbufferTarget ? 0L : this.targetTexture,
                this.targetMip,
                vertexOffset,
                vertexCount,
                blurConfig,
                postGlobals);
            if (!ok) {
                throw unsupported("draw(native post pass failed)");
            }
            return;
        }
        // ponytail: remaining non-indexed draws are world/post work (entity_outline_blit and
        // friends); skip until those backends exist.
        logSkippedPipeline("draw");
    }

    @Override
    public void close() {
        if (this.closed) {
            return;
        }
        if (this.pushedDebugGroups > 0) {
            throw new IllegalStateException("Render pass had debug groups left open");
        }
        this.closed = true;
    }

    private void checkOpen() {
        if (this.closed) {
            throw new IllegalStateException("Can't use a closed render pass");
        }
    }

    private IllegalStateException unsupported(String operation) {
        String pipelineName = this.pipeline == null ? "<no pipeline>" : this.pipeline.getLocation().toString();
        return D3D11RenderSystem.unsupported("RenderPass." + operation + " pipeline=" + pipelineName);
    }

    private void logSkippedPipeline(String operation) {
        String location = pipelineLocation();
        if (loggedSkippedPipelines.add(operation + "|" + location)) {
            D3D11ModInitializer.LOGGER.warn("Skipping unimplemented D3D11 RenderPass.{} pipeline={}", operation, location.isEmpty() ? "<no pipeline>" : location);
        }
    }

    private String pipelineLocation() {
        return this.pipeline == null ? "" : this.pipeline.getLocation().toString();
    }

    private boolean isSupportedGuiPipeline() {
        String location = pipelineLocation();
        return GUI_PIPELINE.equals(location)
            || GUI_TEXTURED_PIPELINE.equals(location)
            || GUI_TEXT_PIPELINE.equals(location)
            || GUI_TEXT_HIGHLIGHT_PIPELINE.equals(location)
            || GUI_TEXTURED_PREMULTIPLIED_PIPELINE.equals(location)
            || GUI_OPAQUE_TEXTURED_BACKGROUND_PIPELINE.equals(location)
            || GUI_NAUSEA_OVERLAY_PIPELINE.equals(location)
            || MOJANG_LOGO_PIPELINE.equals(location)
            || CROSSHAIR_PIPELINE.equals(location)
            || VIGNETTE_PIPELINE.equals(location);
    }

    private boolean isGuiTextPipeline() {
        return GUI_TEXT_PIPELINE.equals(pipelineLocation());
    }

    private int blendMode() {
        String location = pipelineLocation();
        if (MOJANG_LOGO_PIPELINE.equals(location) || GUI_NAUSEA_OVERLAY_PIPELINE.equals(location)) {
            return BLEND_ADDITIVE_ALPHA;
        }
        if (GUI_TEXTURED_PREMULTIPLIED_PIPELINE.equals(location)) {
            return BLEND_PREMULTIPLIED;
        }
        if (GUI_OPAQUE_TEXTURED_BACKGROUND_PIPELINE.equals(location)) {
            return BLEND_NONE;
        }
        if (CROSSHAIR_PIPELINE.equals(location)) {
            return BLEND_INVERT;
        }
        if (VIGNETTE_PIPELINE.equals(location)) {
            return BLEND_MULTIPLY_DARKEN;
        }
        return BLEND_TRANSLUCENT;
    }

    private ByteBuffer uniformData(String name) {
        GpuBufferSlice slice = this.uniforms.get(name);
        if (slice == null) {
            throw unsupported("drawIndexed(missing uniform " + name + ")");
        }
        return sliceData(slice, name);
    }

    private ByteBuffer sliceData(GpuBufferSlice slice, String name) {
        if (!(slice.buffer() instanceof D3D11GpuBuffer buffer)) {
            throw unsupported("drawIndexed(foreign uniform " + name + ")");
        }
        ByteBuffer data = buffer.data().duplicate();
        int start = Math.toIntExact(slice.offset());
        data.position(start);
        data.limit(Math.toIntExact(start + slice.length()));
        return data.slice();
    }

    private long boundTextureHandle() {
        TextureAndSampler binding = this.samplers.get("Sampler0");
        if (binding == null && !this.samplers.isEmpty()) {
            binding = this.samplers.values().iterator().next();
        }
        if (binding == null || !(binding.view().texture() instanceof D3D11GpuTexture texture) || !texture.hasNativeHandle()) {
            throw unsupported("drawIndexed(missing native texture)");
        }
        return texture.nativeHandle();
    }

    // The bound GpuSampler mag filter forwards to native as psMode bit 0x100.
    private boolean boundSamplerIsLinear() {
        TextureAndSampler binding = this.samplers.get("Sampler0");
        if (binding == null && !this.samplers.isEmpty()) {
            binding = this.samplers.values().iterator().next();
        }
        return binding != null
            && binding.sampler() instanceof D3D11Sampler sampler
            && sampler.getMagFilter() == FilterMode.LINEAR;
    }
    private long samplerTextureHandle(String name) {
        TextureAndSampler binding = this.samplers.get(name);
        if (binding == null || !(binding.view().texture() instanceof D3D11GpuTexture texture) || !texture.hasNativeHandle()) {
            throw unsupported("draw(missing native texture for sampler " + name + ")");
        }
        return texture.nativeHandle();
    }

    private long optionalSamplerTextureHandle(String name) {
        TextureAndSampler binding = this.samplers.get(name);
        if (binding == null || !(binding.view().texture() instanceof D3D11GpuTexture texture) || !texture.hasNativeHandle()) {
            return 0L;
        }
        return texture.nativeHandle();
    }

    private @Nullable ByteBuffer optionalUniformData(String name) {
        GpuBufferSlice slice = this.uniforms.get(name);
        if (slice == null || !(slice.buffer() instanceof D3D11GpuBuffer)) {
            return null;
        }
        return sliceData(slice, name);
    }

    private void logFirstGuiInputs(D3D11GpuBuffer vertexBuffer, D3D11GpuBuffer indexBuffer, ByteBuffer dynamicTransforms, ByteBuffer projection, int vertexOffset, int firstIndex, int indexCount, VertexFormat.IndexType indexType, int stride, int uvOffset, int colorOffset, boolean textured) {
        if (textured ? loggedFirstTexturedGuiDrawInputs : loggedFirstGuiDrawInputs) {
            return;
        }
        if (textured) {
            loggedFirstTexturedGuiDrawInputs = true;
        } else {
            loggedFirstGuiDrawInputs = true;
        }
        D3D11ModInitializer.LOGGER.warn(
            "D3D11 first {}gui draw pipeline={} stride={} uvOffset={} colorOffset={} vertexOffset={} firstIndex={} indexCount={} vertex={} indices={} colorMod={} proj={}",
            textured ? "textured " : "",
            this.pipeline.getLocation(),
            stride,
            uvOffset,
            colorOffset,
            vertexOffset,
            firstIndex,
            indexCount,
            describeVertex(vertexBuffer.data(), Math.max(0, vertexOffset) * stride, stride, uvOffset, colorOffset),
            describeIndices(indexBuffer.data(), firstIndex, Math.min(indexCount, 6), indexType),
            describeFloats(dynamicTransforms, 64, 4),
            describeFloats(projection, 0, 16)
        );
    }

    private static String describeVertex(ByteBuffer source, int offset, int stride, int uvOffset, int colorOffset) {
        ByteBuffer data = source.duplicate().order(ByteOrder.nativeOrder());
        if (offset < 0 || offset + stride > data.capacity()) {
            return "<out-of-range>";
        }
        float x = data.getFloat(offset);
        float y = data.getFloat(offset + 4);
        float z = data.getFloat(offset + 8);
        String color = describeColor(data, offset + colorOffset);
        if (uvOffset < 0) {
            return String.format("pos=(%.3f,%.3f,%.3f) color=%s", x, y, z, color);
        }
        float u = data.getFloat(offset + uvOffset);
        float v = data.getFloat(offset + uvOffset + 4);
        return String.format("pos=(%.3f,%.3f,%.3f) uv=(%.3f,%.3f) color=%s", x, y, z, u, v, color);
    }

    private static String describeColor(ByteBuffer data, int offset) {
        if (offset < 0 || offset + 4 > data.capacity()) {
            return "<out-of-range>";
        }
        return String.format("(%d,%d,%d,%d)", Byte.toUnsignedInt(data.get(offset)), Byte.toUnsignedInt(data.get(offset + 1)), Byte.toUnsignedInt(data.get(offset + 2)), Byte.toUnsignedInt(data.get(offset + 3)));
    }

    private static String describeIndices(ByteBuffer source, int firstIndex, int count, VertexFormat.IndexType indexType) {
        ByteBuffer data = source.duplicate().order(ByteOrder.nativeOrder());
        int offset = firstIndex * indexType.bytes;
        if (offset < 0 || offset + count * indexType.bytes > data.capacity()) {
            return "<out-of-range>";
        }
        StringBuilder out = new StringBuilder("[");
        for (int i = 0; i < count; i++) {
            if (i > 0) {
                out.append(',');
            }
            out.append(indexType.bytes == 2 ? Short.toUnsignedInt(data.getShort(offset + i * 2)) : data.getInt(offset + i * 4));
        }
        return out.append(']').toString();
    }

    private static String describeFloats(ByteBuffer source, int offset, int count) {
        ByteBuffer data = source.duplicate().order(ByteOrder.nativeOrder());
        if (offset < 0 || offset + count * Float.BYTES > data.capacity()) {
            return "<out-of-range>";
        }
        StringBuilder out = new StringBuilder("[");
        for (int i = 0; i < count; i++) {
            if (i > 0) {
                out.append(',');
            }
            out.append(String.format("%.3f", data.getFloat(offset + i * Float.BYTES)));
        }
        return out.append(']').toString();
    }

    private record TextureAndSampler(GpuTextureView view, GpuSampler sampler) {
    }
}
