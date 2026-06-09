// Zues standalone runtime (`<project>.exe`).
//
// Tiny main loop that:
//   1. engine_startup() loads modules (window, renderer, physics) from the
//      same dir as this exe.
//   2. Creates a World, registers builtins, wires HostContext + HostApi.
//   3. Loads the project DLL via ProjectDllLoader -> on_load registers
//      project components, systems, singletons, timers.
//   4. Loads the project's default world JSON.
//   5. Runs the standard phase + physics + render loop; no editor UI.
//   6. Closes when the OS window closes.
//
// Reads the .zuesproject sitting next to the exe (or via --project=<file>).
// All the runtime-side logic lives in HostShared -- this file is just the
// boot + loop.

#include <zues/asset.h>
#include <zues/engine.h>
#include <zues/events.h>
#include <zues/log.h>
#include <zues/project.h>
#include <zues/service.h>

#include <zues/services/physics.h>
#include <zues/services/render_camera.h>
#include <zues/services/renderer_2d.h>
#include <zues/services/window.h>

#include <zues/components/render.h>
#include <zues/components/transform.h>

#include <zues/host/host_api.h>
#include <zues/host/host_context.h>
#include <zues/host/path_util.h>
#include <zues/host/project_loader.h>
#include <zues/host/render_camera_service.h>
#include <zues/host/animator_system.h>
#include <zues/host/particle_system.h>
#include <zues/host/audio_system.h>
#include <zues/host/sprite_render_system.h>
#include <zues/host/ui_render_system.h>

#include <zues/ecs/world.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

fs::path executable_dir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return fs::current_path();
    return fs::path(buf).parent_path();
#else
    return fs::current_path();
#endif
}

// Find the .zuesproject. Resolution order:
//   --project=<path> arg if present
//   first *.zuesproject in the exe directory (typical packaged layout)
//   first *.zuesproject in the current working directory
std::string resolve_project_path(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--project=", 10) == 0) return argv[i] + 10;
    }
    std::error_code ec;
    const fs::path candidates[] = { executable_dir(), fs::current_path(ec) };
    for (const auto& dir : candidates) {
        if (dir.empty() || !fs::exists(dir, ec)) continue;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() == ".zuesproject")
                return Engine::host::path_str(e.path());
        }
    }
    return {};
}

bool load_world_from_disk(Engine::ecs::World& world, const fs::path& abs) {
    std::error_code ec;
    if (!fs::exists(abs, ec) || !fs::is_regular_file(abs, ec)) return false;
    std::ifstream in(abs, std::ios::binary | std::ios::ate);
    if (!in) return false;
    auto sz = in.tellg();
    in.seekg(0);
    std::vector<unsigned char> bytes(static_cast<size_t>(sz));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), sz)) return false;
    char first = 0;
    for (auto b : bytes) {
        if (b!=' '&&b!='\t'&&b!='\n'&&b!='\r') { first = (char)b; break; }
    }
    world.clear();
    const Engine::Result res = (first == '{')
        ? world.load_json(reinterpret_cast<const char*>(bytes.data()), bytes.size())
        : world.load_binary(bytes.data(), bytes.size());
    return res == Engine::Result::Ok;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace Engine;

    // ---- Resolve project file -------------------------------------------
    const std::string project_path = resolve_project_path(argc, argv);
    if (project_path.empty()) {
        std::fprintf(stderr, "[runtime] no .zuesproject found next to "
                              "the exe or in cwd; pass --project=<file>\n");
        return 1;
    }

    Project project;
    if (load_project(project_path.c_str(), project) != Result::Ok) {
        std::fprintf(stderr, "[runtime] failed to load project: %s\n",
                     project_path.c_str());
        return 1;
    }

    // ---- Boot the engine + modules --------------------------------------
    const auto exe_dir     = executable_dir();
    const auto exe_dir_str = exe_dir.string();

    EngineStartupDesc desc = {};
    desc.modules_dir = exe_dir_str.c_str();
    desc.project_dll = "";
    if (engine_startup(desc) != Result::Ok) {
        std::fprintf(stderr, "[runtime] engine_startup failed\n");
        return 1;
    }
    host::register_render_camera_service();

    auto* window = static_cast<IWindow_v1*>(
        services()->get_service(ZUES_SERVICE_WINDOW_V1, ZUES_SERVICE_WINDOW_V1_VERSION));
    auto* renderer = static_cast<IRenderer_2D_v1*>(
        services()->get_service(ZUES_SERVICE_RENDERER_2D, ZUES_SERVICE_RENDERER_2D_VERSION));
    if (!window || !renderer) {
        std::fprintf(stderr, "[runtime] window/renderer service missing\n");
        engine_shutdown();
        return 1;
    }

    // ---- Apply project window settings ----------------------------------
    // The window module creates a default 1280x720 resizable window in
    // on_load (well before we've parsed the .zuesproject). Reconfigure now
    // that we know what the project asked for. Order: title -> size ->
    // resizable -> aspect -> fullscreen, so fullscreen sees the final
    // (w,h) when picking the closest video mode.
    {
        const auto& ps = project.settings;
        if (window->set_title)
            window->set_title(window,
                ps.window_title.empty() ? project.name.c_str()
                                        : ps.window_title.c_str());
        const int w = ps.window_width  > 0 ? ps.window_width  : 1280;
        const int h = ps.window_height > 0 ? ps.window_height : 720;
        if (window->set_size) window->set_size(window, w, h);
        // Fullscreen implies "no user resize" anyway; collapse the two
        // into a single resizable hint for windowed mode.
        if (window->set_resizable)
            window->set_resizable(window, (ps.fixed_size || ps.fullscreen) ? 0 : 1);
        // Force the user-drag aspect ratio to match window_width:height,
        // so resizing the window doesn't stretch sprites/HUD off-pattern.
        // Skip in fullscreen (no user resize there) and when fixed_size
        // already pins both dimensions.
        if (window->set_aspect_ratio) {
            if (!ps.fullscreen && !ps.fixed_size && w > 0 && h > 0) {
                window->set_aspect_ratio(window, w, h);
            } else {
                window->set_aspect_ratio(window, 0, 0);
            }
        }
        if (window->set_fullscreen && ps.fullscreen)
            window->set_fullscreen(window, 1, w, h);
    }

    // ---- World + host context -------------------------------------------
    ecs::World world;
    world.register_builtins();
    // The runtime is always "playing" -- there is no Edit mode here. Game-
    // domain systems registered by the project DLL only tick when the world
    // is in Play; if we forget this, sprites sit there but nothing moves
    // and OnUpdate never fires.
    world.set_tick_mode(ecs::TickMode::Play);

    // Index every asset under <project>/assets so guid-keyed lookups
    // (prefab spawn by guid, sprite Texture refs, etc.) resolve. Without
    // this the host's prefab/asset thunks return null and any
    // `Spawn<Prefab>(<guid>)` call from the project DLL becomes a silent
    // no-op. Editor does the same rescan at boot.
    {
        const auto assets_root = fs::path(project.project_dir) / "assets";
        const Engine::u32 n =
            AssetRegistry::instance().rescan(assets_root.string().c_str());
        char msg[256];
        std::snprintf(msg, sizeof(msg),
            "asset_registry: indexed %u assets under %s",
            n, assets_root.string().c_str());
        log_write(LogLevel::Info, "runtime", msg);
    }

    host::HostContext host_ctx;
    host_ctx.world          = &world;
    host_ctx.project_dir    = project.project_dir;
    host_ctx.project_loaded = true;
    host::set_host_context(&host_ctx);

    const ZuesHostApi host_api = host::build_host_api();
    ZuesEngine* engine_h        = host::engine_handle();

    // ---- Project DLL ----------------------------------------------------
    fs::path dll_abs = project.build.dll_path.empty()
        ? fs::path()
        : (fs::path(project.project_dir) / project.build.dll_path);
    host::ProjectDllLoader project_loader;
    if (dll_abs.empty() || !project_loader.load(dll_abs, &host_api, engine_h)) {
        std::fprintf(stderr, "[runtime] project DLL failed to load: %s\n",
                     dll_abs.string().c_str());
        engine_shutdown();
        return 1;
    }

    // ---- AssetRegistry handle resolver ---------------------------------
    // Same lazy-load path the editor uses: when world load encounters a
    // saved Sprite::texture guid that hasn't been bound yet, the
    // resolver loads the texture through the renderer and binds it on
    // the spot. Without this the runtime starts with blank sprites.
    {
        struct ResolverCtx { ::IRenderer_2D_v1* r; std::string project_dir; };
        static ResolverCtx g_resolver_ctx{ renderer, project.project_dir };
        Engine::AssetRegistry::instance().set_handle_resolver(
            +[](Engine::AssetKind kind, Engine::Guid g, void* user) -> Engine::u32 {
                if (kind != Engine::AssetKind::Texture) return 0;
                auto* ctx = static_cast<ResolverCtx*>(user);
                if (!ctx || !ctx->r || !ctx->r->load_texture_from_file) return 0;
                const char* path = Engine::AssetRegistry::instance().path_for(g);
                if (!path) return 0;
                std::string abs = ctx->project_dir;
                if (!abs.empty() && abs.back() != '/' && abs.back() != '\\') abs += '/';
                abs += "assets/";
                abs += path;
                const Engine::u32 tex = ctx->r->load_texture_from_file(ctx->r, abs.c_str());
                if (tex) {
                    Engine::AssetRegistry::instance()
                        .bind_runtime_handle(Engine::AssetKind::Texture, tex, g);
                }
                return tex;
            }, &g_resolver_ctx);
    }

    // ---- Default world --------------------------------------------------
    // The .zuesproject stores `default_world` relative to project_dir. After
    // export, project_dir is the dist folder so the same relative resolves
    // there too -- e.g. "assets/worlds/Main.zworld" -> dist/<Name>/assets/...
    if (!project.default_world.empty()) {
        const fs::path world_abs = fs::path(project.project_dir) / project.default_world;
        if (!load_world_from_disk(world, world_abs)) {
            const std::string msg = "default_world load failed: " +
                                     world_abs.string() +
                                     " (running with empty world)";
            log_write(LogLevel::Warn, "runtime", msg.c_str());
        } else {
            const std::string msg = "default_world loaded: " + world_abs.string();
            log_write(LogLevel::Info, "runtime", msg.c_str());
        }
        host::resync_singletons();
    } else {
        log_write(LogLevel::Warn, "runtime",
            "project.default_world is empty -- nothing to load");
    }

    // ---- Sprite render system ------------------------------------------
    host::SpriteRenderSystem sprite_sys;
    sprite_sys.register_into(world, renderer);

    // ---- Animator system (Game-domain, ticks during Play) --------------
    host::AnimatorSystem animator_sys;
    animator_sys.register_into(world, renderer);

    // ---- Particle system (CPU tier; Game update + Both render) --------
    host::ParticleSystem particle_sys;
    particle_sys.register_into(world, renderer);

    // ---- Audio system (miniaudio backend; ECS-driven 2D + 3D mixer) ---
    host::AudioSystem audio_sys;
    audio_sys.init(world);

    // ---- UI render system (HUD: UIAnchor + Text / Sprite) --------------
    // Default-font search order: <project>/assets, then the engine's own
    // bundled assets sit-side, then the exe dir as a final fallback. The
    // dist exporter copies the project's assets/ verbatim, so that's the
    // primary lookup in shipped builds.
    host::UIRenderSystem ui_sys;
    {
        const std::string proj_assets = (fs::path(project.project_dir) / "assets").string();
        const std::string exe_assets  = (exe_dir / "assets").string();
        ui_sys.register_into(world, renderer,
            { proj_assets, exe_assets });
    }

    // ---- Physics service handle for the per-frame step ------------------
    auto* phys_svc = static_cast<IPhysics_v1*>(
        services()->get_service(ZUES_SERVICE_PHYSICS, ZUES_SERVICE_PHYSICS_VERSION));

    // Wire collision callbacks into the project's hooks. The editor does
    // the same dance during Play; the runtime is always "playing".
    if (phys_svc && phys_svc->set_collision_handlers && project_loader.api()) {
        const ZuesProjectApi* papi = project_loader.api();
        ZuesEngine* peng           = project_loader.engine();
        struct Bridge { const ZuesProjectApi* api; ZuesEngine* eng; };
        static Bridge br{papi, peng};
        phys_svc->set_collision_handlers(phys_svc, &br,
            +[](void* u, uint32_t ai, uint32_t ag, uint32_t bi, uint32_t bg) {
                auto* x = static_cast<Bridge*>(u);
                ZuesEntity a{ai, ag}; ZuesEntity b{bi, bg};
                if (x->api->on_collision) x->api->on_collision(x->eng, a, b);
            },
            +[](void* u, uint32_t si, uint32_t sg, uint32_t oi, uint32_t og) {
                auto* x = static_cast<Bridge*>(u);
                ZuesEntity self{si, sg}; ZuesEntity other{oi, og};
                if (x->api->on_trigger_enter) x->api->on_trigger_enter(x->eng, self, other);
            },
            +[](void* u, uint32_t si, uint32_t sg, uint32_t oi, uint32_t og) {
                auto* x = static_cast<Bridge*>(u);
                ZuesEntity self{si, sg}; ZuesEntity other{oi, og};
                if (x->api->on_trigger_exit) x->api->on_trigger_exit(x->eng, self, other);
            });
    }

    // Publish camera every frame to the IRenderCamera_v1 service so the
    // sprite render system has the right view. The runtime's "active
    // camera" is whichever entity has Camera2D::is_active=true (first
    // wins, same rule as the editor's Game panel).
    auto* cam_svc = static_cast<IRenderCamera_v1*>(
        services()->get_service(ZUES_SERVICE_RENDER_CAMERA, ZUES_SERVICE_RENDER_CAMERA_VERSION));

    auto find_active_camera = [&](float& out_pan_x, float& out_pan_y,
                                    float& out_ortho, int& out_sort) -> bool {
        const auto cam_id   = world.find_component_id("Camera2D");
        const auto xform_id = world.find_component_id("Transform2D");
        if (!cam_id || !xform_id) return false;
        struct Ctx { float* px; float* py; float* o; int* sm; bool found; };
        Ctx ctx{ &out_pan_x, &out_pan_y, &out_ortho, &out_sort, false };
        const Engine::ecs::ComponentId required[] = {xform_id, cam_id};
        world.iterate_query(required, 2, nullptr, 0,
            +[](void* u, Engine::ecs::Entity, void** cols, Engine::u32) {
                auto* c   = static_cast<Ctx*>(u);
                auto* tr  = static_cast<Engine::components::Transform2D*>(cols[0]);
                auto* cam = static_cast<Engine::components::Camera2D*>(cols[1]);
                if (!cam->is_active || c->found) return;
                c->found = true;
                *c->px   = tr->position.x;
                *c->py   = tr->position.y;
                *c->o    = cam->ortho_size > 0.0f ? cam->ortho_size : 10.0f;
                *c->sm   = static_cast<int>(cam->sort_mode);
            }, &ctx);
        return ctx.found;
    };

    // ---- Main loop ------------------------------------------------------
    auto t_prev = std::chrono::high_resolution_clock::now();
    while (!window->should_close(window)) {
        window->poll_events(window);
        const auto t_now = std::chrono::high_resolution_clock::now();
        const float dt   = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;

        // Per-frame project on_update.
        project_loader.tick(dt);

        // Refresh the camera service so sprite_render_system sees the
        // current view transform.
        if (cam_svc) {
            float px = 0, py = 0, ortho = 10.0f;
            int   sort = 0;
            const bool have_cam = find_active_camera(px, py, ortho, sort);
            int vw = 0, vh = 0;
            if (window->get_size) window->get_size(window, &vw, &vh);
            if (vh <= 0) vh = 720;
            ZuesRenderCamera cam{};
            cam.pan_x           = px;
            cam.pan_y           = py;
            cam.zoom            = 1.0f;
            cam.rotation        = 0.0f;
            cam.pixels_per_unit = static_cast<float>(vh) / ortho;
            cam.viewport_w      = vw;
            cam.viewport_h      = vh;
            cam.sort_mode       = sort;
            cam_svc->set_active(cam_svc, &cam);
            (void)have_cam;
        }

        // Phase pipeline: Input -> PreUpdate -> (timers) -> Physics ->
        // Update -> PostUpdate -> Render. Same shape the editor uses
        // when in Play mode.
        for (Engine::u32 p = 0; p < static_cast<Engine::u32>(Engine::ecs::Phase::Render); ++p) {
            if (phys_svc && static_cast<Engine::ecs::Phase>(p) == Engine::ecs::Phase::Physics) {
                phys_svc->pre_step (phys_svc, &world, dt);
                phys_svc->step     (phys_svc, dt);
                phys_svc->post_step(phys_svc, &world, dt);
            }
            if (static_cast<Engine::ecs::Phase>(p) == Engine::ecs::Phase::PreUpdate) {
                host::tick_timers(dt);
            }
            world.tick_phase(static_cast<Engine::ecs::Phase>(p), dt);
        }

        // Render pass goes straight to the swapchain; no game RT, no
        // ImGui, no panels. HUD (UIAnchor + Text / Sprite) is drawn by the
        // ui_render_system registered into the world's Render phase.
        renderer->begin_frame(renderer, 0.04f, 0.05f, 0.07f, 1.0f);
        world.tick_phase(Engine::ecs::Phase::Render, dt);
        renderer->end_frame(renderer);
        window->swap_buffers(window);
    }

    // ---- Shutdown -------------------------------------------------------
    project_loader.unload();
    audio_sys.shutdown();
    ui_sys.unregister_from(world);
    sprite_sys.unregister_from(world);
    host::set_host_context(nullptr);
    engine_shutdown();
    return 0;
}
