// ZuesEngine Project Tracker - Default Data

window.defaultData = {
    tasks: [
        // ═══════════════════════════════════════════════════════════════════
        // NETWORKING SYSTEM - Primary Focus for Multiplayer Engine
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "networking",
            title: "Networking System",
            category: "networking",
            priority: "critical",
            completed: false,
            notes: "Core networking infrastructure for multiplayer games. This is the primary differentiator of ZuesEngine.",
            children: [
                {
                    id: "n1",
                    title: "ENet Integration",
                    priority: "high",
                    completed: true,
                    notes: "Basic client/server networking wrapper using ENet UDP library",
                    children: [
                        { id: "n1a", title: "ENet initialization and cleanup", completed: true, children: [] },
                        { id: "n1b", title: "Host/Server creation", completed: true, children: [] },
                        { id: "n1c", title: "Client connection", completed: true, children: [] },
                        { id: "n1d", title: "Basic send/receive", completed: true, children: [] }
                    ]
                },
                {
                    id: "n2",
                    title: "Connection Management",
                    priority: "critical",
                    completed: false,
                    notes: "Robust connection handling for reliable multiplayer",
                    children: [
                        { id: "n2a", title: "Client connection state machine (Disconnected, Connecting, Connected, Disconnecting)", completed: false, children: [] },
                        { id: "n2b", title: "Connection timeout handling", completed: false, children: [] },
                        { id: "n2c", title: "Automatic reconnection logic", completed: false, children: [] },
                        { id: "n2d", title: "Graceful disconnect with cleanup", completed: false, children: [] },
                        { id: "n2e", title: "Connection events (OnConnected, OnDisconnected, OnConnectionFailed)", completed: false, children: [] },
                        { id: "n2f", title: "Client ID assignment and management", completed: false, children: [] },
                        { id: "n2g", title: "Maximum players enforcement", completed: false, children: [] },
                        { id: "n2h", title: "Kick/ban functionality", completed: false, children: [] }
                    ]
                },
                {
                    id: "n3",
                    title: "Network Message System",
                    priority: "critical",
                    completed: false,
                    notes: "Structured message passing with type safety",
                    children: [
                        { id: "n3a", title: "Message header structure (type, size, sequence)", completed: false, children: [] },
                        { id: "n3b", title: "Message type registry", completed: false, children: [] },
                        { id: "n3c", title: "Binary serialization for primitives (int, float, string, vec2, vec3)", completed: false, children: [] },
                        { id: "n3d", title: "Message pooling to avoid allocations", completed: false, children: [] },
                        { id: "n3e", title: "Reliable vs unreliable channels", completed: false, children: [] },
                        { id: "n3f", title: "Message compression (optional)", completed: false, children: [] },
                        { id: "n3g", title: "Sequence numbers for ordering", completed: false, children: [] },
                        { id: "n3h", title: "Message fragmentation for large payloads", completed: false, children: [] }
                    ]
                },
                {
                    id: "n4",
                    title: "Entity Replication",
                    priority: "critical",
                    completed: false,
                    notes: "Automatic synchronization of entities and components across network",
                    children: [
                        { id: "n4a", title: "NetworkID component (unique network identifier)", completed: false, children: [] },
                        { id: "n4b", title: "NetworkID allocation (server-authoritative)", completed: false, children: [] },
                        { id: "n4c", title: "ReplicatedComponent marker trait", completed: false, children: [] },
                        { id: "n4d", title: "Component replication registry", completed: false, children: [] },
                        { id: "n4e", title: "Spawn message (create entity on clients)", completed: false, children: [] },
                        { id: "n4f", title: "Despawn message (destroy entity on clients)", completed: false, children: [] },
                        { id: "n4g", title: "Component update messages", completed: false, children: [] },
                        { id: "n4h", title: "Delta compression (only send changed values)", completed: false, children: [] },
                        { id: "n4i", title: "Snapshot system (full state sync)", completed: false, children: [] },
                        { id: "n4j", title: "Priority-based replication (important entities first)", completed: false, children: [] },
                        { id: "n4k", title: "Ownership system (who controls this entity)", completed: false, children: [] },
                        { id: "n4l", title: "Authority transfer between clients", completed: false, children: [] }
                    ]
                },
                {
                    id: "n5",
                    title: "RPC System (Remote Procedure Calls)",
                    priority: "critical",
                    completed: false,
                    notes: "Call functions on remote machines with automatic serialization",
                    children: [
                        { id: "n5a", title: "RPC function registry", completed: false, children: [] },
                        { id: "n5b", title: "Argument serialization (auto-serialize params)", completed: false, children: [] },
                        { id: "n5c", title: "ServerRPC - client calls function on server", completed: false, children: [] },
                        { id: "n5d", title: "ClientRPC - server calls function on specific client", completed: false, children: [] },
                        { id: "n5e", title: "MulticastRPC - server calls function on all clients", completed: false, children: [] },
                        { id: "n5f", title: "RPC validation (is caller allowed?)", completed: false, children: [] },
                        { id: "n5g", title: "RPC rate limiting", completed: false, children: [] },
                        { id: "n5h", title: "Macro/template for easy RPC declaration", completed: false, children: [] }
                    ]
                },
                {
                    id: "n6",
                    title: "Client-Side Prediction",
                    priority: "high",
                    completed: false,
                    notes: "Responsive controls despite network latency",
                    children: [
                        { id: "n6a", title: "Input buffering (store local inputs)", completed: false, children: [] },
                        { id: "n6b", title: "Input timestamping", completed: false, children: [] },
                        { id: "n6c", title: "Predicted state simulation", completed: false, children: [] },
                        { id: "n6d", title: "Server state acknowledgment", completed: false, children: [] },
                        { id: "n6e", title: "State reconciliation (fix mispredictions)", completed: false, children: [] },
                        { id: "n6f", title: "Smoothed correction (avoid visual pops)", completed: false, children: [] },
                        { id: "n6g", title: "Prediction error threshold tuning", completed: false, children: [] }
                    ]
                },
                {
                    id: "n7",
                    title: "Entity Interpolation",
                    priority: "high",
                    completed: false,
                    notes: "Smooth movement of remote entities",
                    children: [
                        { id: "n7a", title: "Snapshot buffer (store recent states)", completed: false, children: [] },
                        { id: "n7b", title: "Interpolation delay calculation", completed: false, children: [] },
                        { id: "n7c", title: "Linear interpolation for positions", completed: false, children: [] },
                        { id: "n7d", title: "Spherical interpolation for rotations", completed: false, children: [] },
                        { id: "n7e", title: "Extrapolation for late packets", completed: false, children: [] },
                        { id: "n7f", title: "Jitter buffer management", completed: false, children: [] },
                        { id: "n7g", title: "Visual-only interpolation (physics at server rate)", completed: false, children: [] }
                    ]
                },
                {
                    id: "n8",
                    title: "Network Relevancy & Culling",
                    priority: "medium",
                    completed: false,
                    notes: "Only send data that matters to each client",
                    children: [
                        { id: "n8a", title: "Distance-based relevancy", completed: false, children: [] },
                        { id: "n8b", title: "Relevancy radius per entity type", completed: false, children: [] },
                        { id: "n8c", title: "Always-relevant entities (global objects)", completed: false, children: [] },
                        { id: "n8d", title: "Owner-relevant entities (only owner sees)", completed: false, children: [] },
                        { id: "n8e", title: "Team-based visibility", completed: false, children: [] },
                        { id: "n8f", title: "Dormancy system (pause replication for inactive)", completed: false, children: [] },
                        { id: "n8g", title: "Spatial partitioning for efficient queries", completed: false, children: [] }
                    ]
                },
                {
                    id: "n9",
                    title: "Lag Compensation",
                    priority: "medium",
                    completed: false,
                    notes: "Fair hit detection despite latency differences",
                    children: [
                        { id: "n9a", title: "Server-side position history", completed: false, children: [] },
                        { id: "n9b", title: "Timestamp-based rewind", completed: false, children: [] },
                        { id: "n9c", title: "Hit validation at client's perceived time", completed: false, children: [] },
                        { id: "n9d", title: "Maximum rewind limit (anti-cheat)", completed: false, children: [] }
                    ]
                },
                {
                    id: "n10",
                    title: "Network Statistics & Debugging",
                    priority: "high",
                    completed: false,
                    notes: "Essential for debugging multiplayer issues",
                    children: [
                        { id: "n10a", title: "RTT (round-trip time) measurement", completed: false, children: [] },
                        { id: "n10b", title: "Packet loss tracking", completed: false, children: [] },
                        { id: "n10c", title: "Bandwidth usage monitoring", completed: false, children: [] },
                        { id: "n10d", title: "Network graph overlay (ImGui)", completed: false, children: [] },
                        { id: "n10e", title: "Simulated latency for testing", completed: false, children: [] },
                        { id: "n10f", title: "Simulated packet loss for testing", completed: false, children: [] },
                        { id: "n10g", title: "Message type breakdown (what's using bandwidth)", completed: false, children: [] },
                        { id: "n10h", title: "Entity replication visualizer", completed: false, children: [] }
                    ]
                },
                {
                    id: "n11",
                    title: "Lobby & Matchmaking",
                    priority: "medium",
                    completed: false,
                    notes: "Help players find and join games",
                    children: [
                        { id: "n11a", title: "Lobby data structure (name, map, players, settings)", completed: false, children: [] },
                        { id: "n11b", title: "Create/join/leave lobby", completed: false, children: [] },
                        { id: "n11c", title: "Lobby browser (list available games)", completed: false, children: [] },
                        { id: "n11d", title: "Ready check system", completed: false, children: [] },
                        { id: "n11e", title: "Host migration (if host leaves)", completed: false, children: [] },
                        { id: "n11f", title: "LAN discovery (broadcast/scan)", completed: false, children: [] },
                        { id: "n11g", title: "Steam/Epic integration hooks (future)", completed: false, children: [] }
                    ]
                },
                {
                    id: "n12",
                    title: "Networked Physics",
                    priority: "high",
                    completed: false,
                    notes: "Synchronize physics simulation across network",
                    children: [
                        { id: "n12a", title: "Server-authoritative physics", completed: false, children: [] },
                        { id: "n12b", title: "Physics state replication", completed: false, children: [] },
                        { id: "n12c", title: "Client physics prediction", completed: false, children: [] },
                        { id: "n12d", title: "Physics correction smoothing", completed: false, children: [] },
                        { id: "n12e", title: "Deterministic physics mode (lockstep)", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // RENDERING SYSTEM
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "rendering",
            title: "Rendering System",
            category: "rendering",
            priority: "high",
            completed: false,
            children: [
                {
                    id: "r1",
                    title: "Batch Renderer",
                    priority: "high",
                    completed: true,
                    notes: "OpenGL 3.3+ batch renderer with 20k quads per batch, 32 texture slots",
                    children: [
                        { id: "r1a", title: "Vertex buffer setup", completed: true, children: [] },
                        { id: "r1b", title: "Shader compilation", completed: true, children: [] },
                        { id: "r1c", title: "Texture slot management", completed: true, children: [] },
                        { id: "r1d", title: "Batch flushing logic", completed: true, children: [] }
                    ]
                },
                {
                    id: "r-upgrade",
                    title: "Renderer Upgrades",
                    priority: "high",
                    completed: false,
                    notes: "Modernize and optimize the rendering pipeline",
                    children: [
                        {
                            id: "r-u1",
                            title: "Render Architecture Refactor",
                            priority: "high",
                            completed: false,
                            notes: "Clean up and modularize rendering code",
                            children: [
                                { id: "r-u1a", title: "Abstract renderer interface (prepare for Vulkan/Metal later)", completed: false, children: [] },
                                { id: "r-u1b", title: "Separate render commands from execution", completed: false, children: [] },
                                { id: "r-u1c", title: "Render command queue/buffer", completed: false, children: [] },
                                { id: "r-u1d", title: "Sort-key based rendering (material, depth, texture)", completed: false, children: [] },
                                { id: "r-u1e", title: "Render graph system (dependencies between passes)", completed: false, children: [] },
                                { id: "r-u1f", title: "Multiple render passes support", completed: false, children: [] }
                            ]
                        },
                        {
                            id: "r-u2",
                            title: "Batch Renderer Improvements",
                            priority: "high",
                            completed: false,
                            notes: "Optimize the existing batch renderer",
                            children: [
                                { id: "r-u2a", title: "Persistent mapped buffers (reduce CPU->GPU transfers)", completed: false, children: [] },
                                { id: "r-u2b", title: "Triple buffering for vertex data", completed: false, children: [] },
                                { id: "r-u2c", title: "Bindless textures (eliminate texture slot limit)", completed: false, children: [] },
                                { id: "r-u2d", title: "Instanced rendering for repeated sprites", completed: false, children: [] },
                                { id: "r-u2e", title: "Automatic sprite sorting by texture (reduce batch breaks)", completed: false, children: [] },
                                { id: "r-u2f", title: "Texture array support (single bind for atlases)", completed: false, children: [] },
                                { id: "r-u2g", title: "GPU frustum culling (compute shader)", completed: false, children: [] },
                                { id: "r-u2h", title: "Batch statistics tracking (draw calls, vertices, batches)", completed: false, children: [] }
                            ]
                        },
                        {
                            id: "r-u3",
                            title: "Shader System Upgrade",
                            priority: "high",
                            completed: false,
                            notes: "More flexible and powerful shader handling",
                            children: [
                                { id: "r-u3a", title: "Shader hot-reload (detect file changes)", completed: false, children: [] },
                                { id: "r-u3b", title: "Shader include system (#include support)", completed: false, children: [] },
                                { id: "r-u3c", title: "Shader preprocessor (defines, conditionals)", completed: false, children: [] },
                                { id: "r-u3d", title: "Shader variant/permutation system", completed: false, children: [] },
                                { id: "r-u3e", title: "Shader caching (precompiled binaries)", completed: false, children: [] },
                                { id: "r-u3f", title: "Uniform buffer objects (UBOs) for shared data", completed: false, children: [] },
                                { id: "r-u3g", title: "Material system (shader + properties)", completed: false, children: [] },
                                { id: "r-u3h", title: "Custom shader support for sprites", completed: false, children: [] },
                                { id: "r-u3i", title: "Shader error reporting with line numbers", completed: false, children: [] }
                            ]
                        },
                        {
                            id: "r-u4",
                            title: "Texture System Upgrade",
                            priority: "high",
                            completed: false,
                            notes: "Better texture handling and memory management",
                            children: [
                                { id: "r-u4a", title: "Async texture loading (background thread)", completed: false, children: [] },
                                { id: "r-u4b", title: "Texture streaming (load/unload based on visibility)", completed: false, children: [] },
                                { id: "r-u4c", title: "Texture compression support (DXT/BC, ETC, ASTC)", completed: false, children: [] },
                                { id: "r-u4d", title: "Mipmap generation and support", completed: false, children: [] },
                                { id: "r-u4e", title: "Texture atlasing at runtime (automatic packing)", completed: false, children: [] },
                                { id: "r-u4f", title: "Virtual texturing (mega-texture) for large worlds", completed: false, children: [] },
                                { id: "r-u4g", title: "Texture memory budget tracking", completed: false, children: [] },
                                { id: "r-u4h", title: "Anisotropic filtering options", completed: false, children: [] },
                                { id: "r-u4i", title: "Sampler objects (reusable filtering settings)", completed: false, children: [] }
                            ]
                        },
                        {
                            id: "r-u5",
                            title: "Render Targets & FBOs",
                            priority: "medium",
                            completed: false,
                            notes: "Off-screen rendering capabilities",
                            children: [
                                { id: "r-u5a", title: "RenderTexture class", completed: false, children: [] },
                                { id: "r-u5b", title: "Multiple render targets (MRT)", completed: false, children: [] },
                                { id: "r-u5c", title: "Render target pooling (reuse FBOs)", completed: false, children: [] },
                                { id: "r-u5d", title: "Render to texture for minimaps", completed: false, children: [] },
                                { id: "r-u5e", title: "Screenshot/capture to file", completed: false, children: [] },
                                { id: "r-u5f", title: "Viewport render to texture (for UI previews)", completed: false, children: [] }
                            ]
                        },
                        {
                            id: "r-u6",
                            title: "Blend Modes & Compositing",
                            priority: "medium",
                            completed: false,
                            notes: "Advanced sprite compositing",
                            children: [
                                { id: "r-u6a", title: "Standard blend modes (additive, multiply, screen)", completed: false, children: [] },
                                { id: "r-u6b", title: "Per-sprite blend mode", completed: false, children: [] },
                                { id: "r-u6c", title: "Premultiplied alpha support", completed: false, children: [] },
                                { id: "r-u6d", title: "Custom blend equations", completed: false, children: [] },
                                { id: "r-u6e", title: "Stencil buffer operations", completed: false, children: [] },
                                { id: "r-u6f", title: "Masking/clipping (arbitrary shapes)", completed: false, children: [] }
                            ]
                        },
                        {
                            id: "r-u7",
                            title: "Multi-threading & Performance",
                            priority: "medium",
                            completed: false,
                            notes: "Parallel rendering preparation",
                            children: [
                                { id: "r-u7a", title: "Render thread separation", completed: false, children: [] },
                                { id: "r-u7b", title: "Parallel sprite culling", completed: false, children: [] },
                                { id: "r-u7c", title: "Parallel command buffer generation", completed: false, children: [] },
                                { id: "r-u7d", title: "Double-buffered render state", completed: false, children: [] },
                                { id: "r-u7e", title: "GPU query timers for profiling", completed: false, children: [] },
                                { id: "r-u7f", title: "Frame pacing / vsync options", completed: false, children: [] }
                            ]
                        },
                        {
                            id: "r-u8",
                            title: "Debug & Visualization",
                            priority: "high",
                            completed: false,
                            notes: "Rendering debug tools",
                            children: [
                                { id: "r-u8a", title: "Wireframe mode toggle", completed: false, children: [] },
                                { id: "r-u8b", title: "Overdraw visualization", completed: false, children: [] },
                                { id: "r-u8c", title: "Batch break visualization", completed: false, children: [] },
                                { id: "r-u8d", title: "Texture mip level visualization", completed: false, children: [] },
                                { id: "r-u8e", title: "GPU memory usage display", completed: false, children: [] },
                                { id: "r-u8f", title: "Draw call breakdown by category", completed: false, children: [] },
                                { id: "r-u8g", title: "Frame capture/inspection (like RenderDoc integration)", completed: false, children: [] }
                            ]
                        }
                    ]
                },
                {
                    id: "r2",
                    title: "Text Rendering",
                    priority: "high",
                    completed: true,
                    notes: "STB TrueType font rendering with atlas generation",
                    children: [
                        { id: "r2a", title: "Font loading from TTF", completed: true, children: [] },
                        { id: "r2b", title: "Atlas generation", completed: true, children: [] },
                        { id: "r2c", title: "Character metrics", completed: true, children: [] },
                        { id: "r2d", title: "Text rotation support", completed: true, children: [] }
                    ]
                },
                {
                    id: "r2-upgrade",
                    title: "Text Rendering Upgrades",
                    priority: "medium",
                    completed: false,
                    notes: "Enhanced text capabilities",
                    children: [
                        { id: "r2-u1", title: "SDF (Signed Distance Field) fonts", completed: false, children: [] },
                        { id: "r2-u2", title: "Text outlines and shadows", completed: false, children: [] },
                        { id: "r2-u3", title: "Gradient text colors", completed: false, children: [] },
                        { id: "r2-u4", title: "Rich text markup (<b>, <i>, <color>)", completed: false, children: [] },
                        { id: "r2-u5", title: "Text wrapping and alignment", completed: false, children: [] },
                        { id: "r2-u6", title: "Kerning support", completed: false, children: [] },
                        { id: "r2-u7", title: "Multiple font sizes from single TTF", completed: false, children: [] },
                        { id: "r2-u8", title: "Unicode/emoji support", completed: false, children: [] },
                        { id: "r2-u9", title: "Text effects (wave, shake, typewriter)", completed: false, children: [] },
                        { id: "r2-u10", title: "Dynamic atlas resizing", completed: false, children: [] }
                    ]
                },
                {
                    id: "r3",
                    title: "Camera System",
                    priority: "high",
                    completed: true,
                    notes: "2D camera with zoom, pan, world/screen transforms",
                    children: [
                        { id: "r3a", title: "Orthographic projection", completed: true, children: [] },
                        { id: "r3b", title: "Screen to world conversion", completed: true, children: [] },
                        { id: "r3c", title: "World to screen conversion", completed: true, children: [] },
                        { id: "r3d", title: "Camera follow with smoothing", completed: false, children: [] },
                        { id: "r3e", title: "Camera shake effect", completed: false, children: [] },
                        { id: "r3f", title: "Camera bounds/limits", completed: false, children: [] },
                        { id: "r3g", title: "Split-screen support (multiple cameras)", completed: false, children: [] },
                        { id: "r3h", title: "Camera lerp/slerp transitions", completed: false, children: [] },
                        { id: "r3i", title: "Dead zone for follow targets", completed: false, children: [] },
                        { id: "r3j", title: "Look-ahead based on velocity", completed: false, children: [] },
                        { id: "r3k", title: "Camera priority system (blend between cameras)", completed: false, children: [] },
                        { id: "r3l", title: "Cinemachine-style virtual cameras", completed: false, children: [] }
                    ]
                },
                {
                    id: "r4",
                    title: "Sprite Animation System",
                    priority: "high",
                    completed: false,
                    notes: "Frame-based sprite animation with state machine",
                    children: [
                        { id: "r4a", title: "AnimationClip data structure (frames, duration, loop)", completed: false, children: [] },
                        { id: "r4b", title: "AnimatorComponent (current clip, time, speed)", completed: false, children: [] },
                        { id: "r4c", title: "Animation playback system", completed: false, children: [] },
                        { id: "r4d", title: "Animation events (callback at specific frame)", completed: false, children: [] },
                        { id: "r4e", title: "Animation state machine", completed: false, children: [] },
                        { id: "r4f", title: "Blend transitions between states", completed: false, children: [] },
                        { id: "r4g", title: "Animation file format (.anim)", completed: false, children: [] },
                        { id: "r4h", title: "Editor: Animation timeline", completed: false, children: [] },
                        { id: "r4i", title: "Editor: State machine graph", completed: false, children: [] },
                        { id: "r4j", title: "Animation layers (overlay animations)", completed: false, children: [] },
                        { id: "r4k", title: "Root motion support", completed: false, children: [] },
                        { id: "r4l", title: "Animation curves for properties", completed: false, children: [] },
                        { id: "r4m", title: "Sprite flip without new frames", completed: false, children: [] }
                    ]
                },
                {
                    id: "r5",
                    title: "Particle System",
                    priority: "medium",
                    completed: false,
                    notes: "GPU-friendly particle effects",
                    children: [
                        { id: "r5a", title: "Particle data structure (pos, vel, life, color, size)", completed: false, children: [] },
                        { id: "r5b", title: "Particle pool with recycling", completed: false, children: [] },
                        { id: "r5c", title: "ParticleEmitterComponent", completed: false, children: [] },
                        { id: "r5d", title: "Spawn shapes (point, line, circle, rect)", completed: false, children: [] },
                        { id: "r5e", title: "Spawn rate (particles per second)", completed: false, children: [] },
                        { id: "r5f", title: "Initial velocity (direction + randomness)", completed: false, children: [] },
                        { id: "r5g", title: "Color over lifetime curve", completed: false, children: [] },
                        { id: "r5h", title: "Size over lifetime curve", completed: false, children: [] },
                        { id: "r5i", title: "Gravity/forces", completed: false, children: [] },
                        { id: "r5j", title: "Texture atlas support (animated particles)", completed: false, children: [] },
                        { id: "r5k", title: "Particle collision (optional)", completed: false, children: [] },
                        { id: "r5l", title: "Editor: Particle preview", completed: false, children: [] },
                        { id: "r5m", title: "Preset library (explosion, smoke, fire, sparkles)", completed: false, children: [] },
                        { id: "r5n", title: "GPU particle simulation (compute shader)", completed: false, children: [] },
                        { id: "r5o", title: "Particle trails/ribbons", completed: false, children: [] },
                        { id: "r5p", title: "Sub-emitters (spawn particles on death)", completed: false, children: [] },
                        { id: "r5q", title: "Noise/turbulence forces", completed: false, children: [] },
                        { id: "r5r", title: "Attract/repel forces", completed: false, children: [] }
                    ]
                },
                {
                    id: "r6",
                    title: "Post-Processing",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "r6a", title: "FBO (framebuffer object) system", completed: false, children: [] },
                        { id: "r6b", title: "Post-process stack", completed: false, children: [] },
                        { id: "r6c", title: "Bloom effect", completed: false, children: [] },
                        { id: "r6d", title: "Blur effect (gaussian)", completed: false, children: [] },
                        { id: "r6e", title: "Color grading / LUT", completed: false, children: [] },
                        { id: "r6f", title: "Vignette", completed: false, children: [] },
                        { id: "r6g", title: "Screen shake", completed: false, children: [] },
                        { id: "r6h", title: "CRT/retro effect", completed: false, children: [] },
                        { id: "r6i", title: "Chromatic aberration", completed: false, children: [] },
                        { id: "r6j", title: "Film grain", completed: false, children: [] },
                        { id: "r6k", title: "Pixelation effect", completed: false, children: [] },
                        { id: "r6l", title: "Outline/edge detection", completed: false, children: [] },
                        { id: "r6m", title: "Distortion/heat haze", completed: false, children: [] },
                        { id: "r6n", title: "Fade in/out transitions", completed: false, children: [] },
                        { id: "r6o", title: "Post-process volumes (area-based effects)", completed: false, children: [] }
                    ]
                },
                {
                    id: "r7",
                    title: "Tilemaps",
                    priority: "high",
                    completed: false,
                    notes: "Efficient tile-based level rendering",
                    children: [
                        { id: "r7a", title: "TilemapComponent (grid data)", completed: false, children: [] },
                        { id: "r7b", title: "Tileset definition (tile atlas + metadata)", completed: false, children: [] },
                        { id: "r7c", title: "Efficient tilemap rendering (chunked batching)", completed: false, children: [] },
                        { id: "r7d", title: "Multiple tilemap layers", completed: false, children: [] },
                        { id: "r7e", title: "Auto-tiling (rule-based tile selection)", completed: false, children: [] },
                        { id: "r7f", title: "Animated tiles", completed: false, children: [] },
                        { id: "r7g", title: "Tilemap collision generation", completed: false, children: [] },
                        { id: "r7h", title: "Tiled (.tmx) importer", completed: false, children: [] },
                        { id: "r7i", title: "Editor: Tile palette", completed: false, children: [] },
                        { id: "r7j", title: "Editor: Brush tools (paint, fill, rect)", completed: false, children: [] },
                        { id: "r7k", title: "Isometric tilemap support", completed: false, children: [] },
                        { id: "r7l", title: "Hexagonal tilemap support", completed: false, children: [] },
                        { id: "r7m", title: "Infinite/chunked tilemaps", completed: false, children: [] },
                        { id: "r7n", title: "Tilemap LOD (zoom out simplification)", completed: false, children: [] }
                    ]
                },
                {
                    id: "r8",
                    title: "Lighting (2D)",
                    priority: "low",
                    completed: false,
                    notes: "Dynamic 2D lighting and shadows",
                    children: [
                        { id: "r8a", title: "Point lights", completed: false, children: [] },
                        { id: "r8b", title: "Spot lights", completed: false, children: [] },
                        { id: "r8c", title: "Global/ambient light", completed: false, children: [] },
                        { id: "r8d", title: "Light blending modes", completed: false, children: [] },
                        { id: "r8e", title: "2D shadow casting", completed: false, children: [] },
                        { id: "r8f", title: "Normal map support", completed: false, children: [] },
                        { id: "r8g", title: "Light cookies/textures", completed: false, children: [] },
                        { id: "r8h", title: "Soft shadows", completed: false, children: [] },
                        { id: "r8i", title: "Shadow caster component", completed: false, children: [] },
                        { id: "r8j", title: "Light flickering effects", completed: false, children: [] },
                        { id: "r8k", title: "Day/night cycle support", completed: false, children: [] },
                        { id: "r8l", title: "Deferred lighting (many lights)", completed: false, children: [] }
                    ]
                },
                {
                    id: "r9",
                    title: "Sprite Features",
                    priority: "high",
                    completed: false,
                    notes: "Additional sprite rendering capabilities",
                    children: [
                        { id: "r9a", title: "9-slice/9-patch sprites (UI scaling)", completed: false, children: [] },
                        { id: "r9b", title: "Sprite outlines", completed: false, children: [] },
                        { id: "r9c", title: "Sprite dissolve/fade shader", completed: false, children: [] },
                        { id: "r9d", title: "Sprite flash/hit effect", completed: false, children: [] },
                        { id: "r9e", title: "Sprite silhouette (behind objects)", completed: false, children: [] },
                        { id: "r9f", title: "Sprite palette swapping", completed: false, children: [] },
                        { id: "r9g", title: "Billboard sprites (always face camera)", completed: false, children: [] },
                        { id: "r9h", title: "Sprite skewing/shearing", completed: false, children: [] },
                        { id: "r9i", title: "Sprite mesh deformation", completed: false, children: [] },
                        { id: "r9j", title: "Sprite pivot point customization", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // SYSTEM IMPROVEMENTS - Make existing systems better
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "improvements",
            title: "System Improvements",
            category: "rendering",
            priority: "high",
            completed: false,
            notes: "Polish and improve existing systems for better developer experience",
            children: [
                {
                    id: "imp-ecs",
                    title: "ECS Improvements",
                    priority: "high",
                    completed: false,
                    notes: "Make the ECS more powerful and easier to use",
                    children: [
                        { id: "imp-ecs1", title: "Component pooling (reduce allocations)", completed: false, children: [] },
                        { id: "imp-ecs2", title: "Archetype chunk iteration (cache-friendly)", completed: false, children: [] },
                        { id: "imp-ecs3", title: "Parallel system execution", completed: false, children: [] },
                        { id: "imp-ecs4", title: "System dependency graph (auto-ordering)", completed: false, children: [] },
                        { id: "imp-ecs5", title: "Entity prefetching hints", completed: false, children: [] },
                        { id: "imp-ecs6", title: "Bulk entity operations (create/destroy many)", completed: false, children: [] },
                        { id: "imp-ecs7", title: "Component change events (OnAdded, OnRemoved, OnChanged)", completed: false, children: [] },
                        { id: "imp-ecs8", title: "Entity versioning for safe handles", completed: false, children: [] },
                        { id: "imp-ecs9", title: "World snapshots (for networking/replay)", completed: false, children: [] },
                        { id: "imp-ecs10", title: "Shared components (same data, multiple entities)", completed: false, children: [] },
                        { id: "imp-ecs11", title: "Chunk components (per-archetype data)", completed: false, children: [] },
                        { id: "imp-ecs12", title: "Query builder API (fluent syntax)", completed: false, children: [] }
                    ]
                },
                {
                    id: "imp-physics",
                    title: "Physics Improvements",
                    priority: "high",
                    completed: false,
                    notes: "Better physics integration and usability",
                    children: [
                        { id: "imp-phy1", title: "Physics body caching (avoid recreation)", completed: false, children: [] },
                        { id: "imp-phy2", title: "Compound collider builder API", completed: false, children: [] },
                        { id: "imp-phy3", title: "Physics debug draw improvements", completed: false, children: [] },
                        { id: "imp-phy4", title: "Collision callbacks with lambda syntax", completed: false, children: [] },
                        { id: "imp-phy5", title: "Physics simulation sub-stepping config", completed: false, children: [] },
                        { id: "imp-phy6", title: "Sleep threshold configuration", completed: false, children: [] },
                        { id: "imp-phy7", title: "Continuous collision detection toggle", completed: false, children: [] },
                        { id: "imp-phy8", title: "Physics query caching", completed: false, children: [] },
                        { id: "imp-phy9", title: "Trigger volume improvements", completed: false, children: [] },
                        { id: "imp-phy10", title: "Physics material assets (reusable)", completed: false, children: [] }
                    ]
                },
                {
                    id: "imp-input",
                    title: "Input System Improvements",
                    priority: "medium",
                    completed: false,
                    notes: "More robust and flexible input handling",
                    children: [
                        { id: "imp-inp1", title: "Input buffering (queue recent inputs)", completed: false, children: [] },
                        { id: "imp-inp2", title: "Input recording and playback", completed: false, children: [] },
                        { id: "imp-inp3", title: "Coyote time helper (platformer grace period)", completed: false, children: [] },
                        { id: "imp-inp4", title: "Input prediction for networking", completed: false, children: [] },
                        { id: "imp-inp5", title: "Combo detection (fighting game inputs)", completed: false, children: [] },
                        { id: "imp-inp6", title: "Gesture detection (swipe, hold, tap)", completed: false, children: [] },
                        { id: "imp-inp7", title: "Input visualization overlay", completed: false, children: [] },
                        { id: "imp-inp8", title: "Per-player input isolation", completed: false, children: [] }
                    ]
                },
                {
                    id: "imp-event",
                    title: "Event System Improvements",
                    priority: "medium",
                    completed: false,
                    notes: "More powerful event handling",
                    children: [
                        { id: "imp-evt1", title: "Event priority ordering", completed: false, children: [] },
                        { id: "imp-evt2", title: "Event consumption (stop propagation)", completed: false, children: [] },
                        { id: "imp-evt3", title: "Deferred event dispatch", completed: false, children: [] },
                        { id: "imp-evt4", title: "Event pooling (reduce allocations)", completed: false, children: [] },
                        { id: "imp-evt5", title: "Weak listener references (auto-cleanup)", completed: false, children: [] },
                        { id: "imp-evt6", title: "Event debugging (log all events)", completed: false, children: [] },
                        { id: "imp-evt7", title: "Event channels/categories", completed: false, children: [] },
                        { id: "imp-evt8", title: "Network event replication", completed: false, children: [] }
                    ]
                },
                {
                    id: "imp-serial",
                    title: "Serialization Improvements",
                    priority: "high",
                    completed: false,
                    notes: "Faster and more robust save/load",
                    children: [
                        { id: "imp-ser1", title: "Binary serialization format", completed: false, children: [] },
                        { id: "imp-ser2", title: "Schema versioning (handle old saves)", completed: false, children: [] },
                        { id: "imp-ser3", title: "Partial world save/load", completed: false, children: [] },
                        { id: "imp-ser4", title: "Async serialization (background thread)", completed: false, children: [] },
                        { id: "imp-ser5", title: "Compression for save files", completed: false, children: [] },
                        { id: "imp-ser6", title: "Entity reference resolution", completed: false, children: [] },
                        { id: "imp-ser7", title: "Custom serializers per component", completed: false, children: [] },
                        { id: "imp-ser8", title: "Diff-based saves (only changes)", completed: false, children: [] }
                    ]
                },
                {
                    id: "imp-asset",
                    title: "Asset System Improvements",
                    priority: "high",
                    completed: false,
                    notes: "Better asset loading and management",
                    children: [
                        { id: "imp-ass1", title: "Asset handle system (indirect references)", completed: false, children: [] },
                        { id: "imp-ass2", title: "Async asset loading", completed: false, children: [] },
                        { id: "imp-ass3", title: "Asset hot-reload (textures, sounds)", completed: false, children: [] },
                        { id: "imp-ass4", title: "Asset dependencies tracking", completed: false, children: [] },
                        { id: "imp-ass5", title: "Asset bundles (group related assets)", completed: false, children: [] },
                        { id: "imp-ass6", title: "Asset memory budget", completed: false, children: [] },
                        { id: "imp-ass7", title: "Asset preloading hints", completed: false, children: [] },
                        { id: "imp-ass8", title: "Asset compression", completed: false, children: [] },
                        { id: "imp-ass9", title: "Asset import pipeline (source -> runtime)", completed: false, children: [] },
                        { id: "imp-ass10", title: "Asset thumbnails for editor", completed: false, children: [] }
                    ]
                },
                {
                    id: "imp-memory",
                    title: "Memory Management",
                    priority: "medium",
                    completed: false,
                    notes: "Better memory usage and tracking",
                    children: [
                        { id: "imp-mem1", title: "Custom allocators (pool, stack, linear)", completed: false, children: [] },
                        { id: "imp-mem2", title: "Memory tracking per system", completed: false, children: [] },
                        { id: "imp-mem3", title: "Memory leak detection", completed: false, children: [] },
                        { id: "imp-mem4", title: "Memory budget enforcement", completed: false, children: [] },
                        { id: "imp-mem5", title: "Object pooling framework", completed: false, children: [] },
                        { id: "imp-mem6", title: "Frame allocator (per-frame temp memory)", completed: false, children: [] },
                        { id: "imp-mem7", title: "Memory defragmentation", completed: false, children: [] }
                    ]
                },
                {
                    id: "imp-threading",
                    title: "Threading & Jobs",
                    priority: "medium",
                    completed: false,
                    notes: "Multi-threaded task execution",
                    children: [
                        { id: "imp-thr1", title: "Job system (task graph execution)", completed: false, children: [] },
                        { id: "imp-thr2", title: "Thread pool", completed: false, children: [] },
                        { id: "imp-thr3", title: "Parallel for loops", completed: false, children: [] },
                        { id: "imp-thr4", title: "Async/await pattern", completed: false, children: [] },
                        { id: "imp-thr5", title: "Main thread task queue", completed: false, children: [] },
                        { id: "imp-thr6", title: "Lock-free data structures", completed: false, children: [] },
                        { id: "imp-thr7", title: "Thread-safe logging", completed: false, children: [] }
                    ]
                },
                {
                    id: "imp-error",
                    title: "Error Handling",
                    priority: "high",
                    completed: false,
                    notes: "Better error reporting and recovery",
                    children: [
                        { id: "imp-err1", title: "Result/Expected type (error handling)", completed: false, children: [] },
                        { id: "imp-err2", title: "Error codes with descriptions", completed: false, children: [] },
                        { id: "imp-err3", title: "Error callback system", completed: false, children: [] },
                        { id: "imp-err4", title: "Graceful degradation (fallback behaviors)", completed: false, children: [] },
                        { id: "imp-err5", title: "Error recovery suggestions", completed: false, children: [] },
                        { id: "imp-err6", title: "Stack traces on errors", completed: false, children: [] },
                        { id: "imp-err7", title: "Error reporting to file/network", completed: false, children: [] }
                    ]
                },
                {
                    id: "imp-config",
                    title: "Configuration System",
                    priority: "medium",
                    completed: false,
                    notes: "Flexible engine and game configuration",
                    children: [
                        { id: "imp-cfg1", title: "Config file format (JSON/TOML)", completed: false, children: [] },
                        { id: "imp-cfg2", title: "Runtime config changes", completed: false, children: [] },
                        { id: "imp-cfg3", title: "Config validation", completed: false, children: [] },
                        { id: "imp-cfg4", title: "Config hot-reload", completed: false, children: [] },
                        { id: "imp-cfg5", title: "Config inheritance (base + override)", completed: false, children: [] },
                        { id: "imp-cfg6", title: "Command-line argument parsing", completed: false, children: [] },
                        { id: "imp-cfg7", title: "Environment variable support", completed: false, children: [] },
                        { id: "imp-cfg8", title: "Config editor UI", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // PHYSICS SYSTEM
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "physics",
            title: "Physics System",
            category: "physics",
            priority: "high",
            completed: false,
            children: [
                {
                    id: "p1",
                    title: "Box2D Integration",
                    priority: "high",
                    completed: true,
                    notes: "Box2D v3 physics simulation with rigidbodies",
                    children: [
                        { id: "p1a", title: "World setup", completed: true, children: [] },
                        { id: "p1b", title: "Body creation from components", completed: true, children: [] },
                        { id: "p1c", title: "Box collider", completed: true, children: [] },
                        { id: "p1d", title: "Circle collider", completed: true, children: [] },
                        { id: "p1e", title: "Transform sync (physics -> ECS)", completed: true, children: [] }
                    ]
                },
                {
                    id: "p2",
                    title: "Collision Events",
                    priority: "critical",
                    completed: false,
                    notes: "Callbacks when collisions occur - essential for gameplay",
                    children: [
                        { id: "p2a", title: "Box2D contact listener implementation", completed: false, children: [] },
                        { id: "p2b", title: "Entity lookup from Box2D body (user data)", completed: false, children: [] },
                        { id: "p2c", title: "CollisionCallbackComponent (store callbacks)", completed: false, children: [] },
                        { id: "p2d", title: "OnCollisionEnter event", completed: false, children: [] },
                        { id: "p2e", title: "OnCollisionStay event", completed: false, children: [] },
                        { id: "p2f", title: "OnCollisionExit event", completed: false, children: [] },
                        { id: "p2g", title: "OnTriggerEnter event", completed: false, children: [] },
                        { id: "p2h", title: "OnTriggerExit event", completed: false, children: [] },
                        { id: "p2i", title: "Collision info struct (other entity, contact point, normal)", completed: false, children: [] }
                    ]
                },
                {
                    id: "p3",
                    title: "Raycasting",
                    priority: "high",
                    completed: false,
                    notes: "Essential for line-of-sight, bullets, ground detection",
                    children: [
                        { id: "p3a", title: "Raycast single (first hit)", completed: false, children: [] },
                        { id: "p3b", title: "Raycast all (all hits)", completed: false, children: [] },
                        { id: "p3c", title: "RaycastHit struct (entity, point, normal, distance)", completed: false, children: [] },
                        { id: "p3d", title: "Layer mask filtering", completed: false, children: [] },
                        { id: "p3e", title: "Box cast", completed: false, children: [] },
                        { id: "p3f", title: "Circle cast", completed: false, children: [] },
                        { id: "p3g", title: "Debug visualization (draw rays)", completed: false, children: [] }
                    ]
                },
                {
                    id: "p4",
                    title: "Physics Layers & Collision Matrix",
                    priority: "high",
                    completed: false,
                    notes: "Control what collides with what",
                    children: [
                        { id: "p4a", title: "Layer definition system (named layers)", completed: false, children: [] },
                        { id: "p4b", title: "Collision matrix (layer vs layer)", completed: false, children: [] },
                        { id: "p4c", title: "Per-collider layer assignment", completed: false, children: [] },
                        { id: "p4d", title: "Editor: Collision matrix UI", completed: false, children: [] },
                        { id: "p4e", title: "Project settings for default layers", completed: false, children: [] }
                    ]
                },
                {
                    id: "p5",
                    title: "Additional Collider Shapes",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "p5a", title: "Capsule collider", completed: false, children: [] },
                        { id: "p5b", title: "Polygon collider (arbitrary convex)", completed: false, children: [] },
                        { id: "p5c", title: "Edge collider (for terrain)", completed: false, children: [] },
                        { id: "p5d", title: "Compound colliders (multiple shapes on one body)", completed: false, children: [] }
                    ]
                },
                {
                    id: "p6",
                    title: "Joints & Constraints",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "p6a", title: "Distance joint (spring)", completed: false, children: [] },
                        { id: "p6b", title: "Revolute joint (hinge)", completed: false, children: [] },
                        { id: "p6c", title: "Prismatic joint (slider)", completed: false, children: [] },
                        { id: "p6d", title: "Weld joint (fixed)", completed: false, children: [] },
                        { id: "p6e", title: "Wheel joint (vehicle)", completed: false, children: [] },
                        { id: "p6f", title: "Mouse joint (drag objects)", completed: false, children: [] },
                        { id: "p6g", title: "Joint motors and limits", completed: false, children: [] },
                        { id: "p6h", title: "Joint break force", completed: false, children: [] }
                    ]
                },
                {
                    id: "p7",
                    title: "Physics Utilities",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "p7a", title: "Apply force/impulse helpers", completed: false, children: [] },
                        { id: "p7b", title: "Set velocity directly", completed: false, children: [] },
                        { id: "p7c", title: "Overlap test (is anything in this area?)", completed: false, children: [] },
                        { id: "p7d", title: "Physics material presets (bouncy, slippery, sticky)", completed: false, children: [] },
                        { id: "p7e", title: "One-way platforms", completed: false, children: [] },
                        { id: "p7f", title: "Effectors (buoyancy zones, wind zones)", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // INPUT SYSTEM
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "input",
            title: "Input System",
            category: "input",
            priority: "high",
            completed: false,
            children: [
                {
                    id: "i1",
                    title: "Keyboard Input",
                    priority: "high",
                    completed: true,
                    notes: "IsKeyPressed, IsKeyJustPressed, IsKeyJustReleased",
                    children: []
                },
                {
                    id: "i2",
                    title: "Mouse Input",
                    priority: "high",
                    completed: true,
                    notes: "Button states, position, screen to world conversion",
                    children: [
                        { id: "i2a", title: "Mouse button states", completed: true, children: [] },
                        { id: "i2b", title: "Mouse position (screen)", completed: true, children: [] },
                        { id: "i2c", title: "Mouse position (world)", completed: true, children: [] },
                        { id: "i2d", title: "Mouse delta (movement)", completed: false, children: [] },
                        { id: "i2e", title: "Mouse scroll wheel", completed: false, children: [] },
                        { id: "i2f", title: "Mouse lock/capture mode", completed: false, children: [] }
                    ]
                },
                {
                    id: "i3",
                    title: "Gamepad Support",
                    priority: "high",
                    completed: false,
                    notes: "Essential for console-style games",
                    children: [
                        { id: "i3a", title: "GLFW joystick detection", completed: false, children: [] },
                        { id: "i3b", title: "Gamepad state polling", completed: false, children: [] },
                        { id: "i3c", title: "Button press/release detection", completed: false, children: [] },
                        { id: "i3d", title: "Analog stick reading (left/right)", completed: false, children: [] },
                        { id: "i3e", title: "Trigger reading (L2/R2)", completed: false, children: [] },
                        { id: "i3f", title: "Deadzone handling (per-stick)", completed: false, children: [] },
                        { id: "i3g", title: "Multi-controller support", completed: false, children: [] },
                        { id: "i3h", title: "Hot-plug detection (connect/disconnect)", completed: false, children: [] },
                        { id: "i3i", title: "Rumble/vibration support", completed: false, children: [] },
                        { id: "i3j", title: "Controller type detection (Xbox/PS/Switch)", completed: false, children: [] }
                    ]
                },
                {
                    id: "i4",
                    title: "Input Action Mapping",
                    priority: "high",
                    completed: false,
                    notes: "Abstract input to actions for rebinding",
                    children: [
                        { id: "i4a", title: "InputAction definition (name, type)", completed: false, children: [] },
                        { id: "i4b", title: "Action types (button, axis, 2D axis)", completed: false, children: [] },
                        { id: "i4c", title: "Binding structure (key/button/axis -> action)", completed: false, children: [] },
                        { id: "i4d", title: "Multiple bindings per action", completed: false, children: [] },
                        { id: "i4e", title: "Input contexts (menu, gameplay, vehicle)", completed: false, children: [] },
                        { id: "i4f", title: "Context stack (push/pop)", completed: false, children: [] },
                        { id: "i4g", title: "Action callbacks (OnPressed, OnReleased, OnHeld)", completed: false, children: [] },
                        { id: "i4h", title: "Composite bindings (WASD -> 2D axis)", completed: false, children: [] },
                        { id: "i4i", title: "Modifiers (shift+key, double-tap)", completed: false, children: [] },
                        { id: "i4j", title: "Save/load bindings to JSON", completed: false, children: [] },
                        { id: "i4k", title: "Editor: Action mapping UI", completed: false, children: [] },
                        { id: "i4l", title: "Runtime rebinding with conflict detection", completed: false, children: [] }
                    ]
                },
                {
                    id: "i5",
                    title: "Touch Input (Future)",
                    priority: "low",
                    completed: false,
                    notes: "For potential mobile support",
                    children: [
                        { id: "i5a", title: "Touch point tracking", completed: false, children: [] },
                        { id: "i5b", title: "Multi-touch support", completed: false, children: [] },
                        { id: "i5c", title: "Gesture recognition (swipe, pinch)", completed: false, children: [] },
                        { id: "i5d", title: "Virtual joystick widget", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // AUDIO SYSTEM
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "audio",
            title: "Audio System",
            category: "audio",
            priority: "high",
            completed: false,
            children: [
                {
                    id: "a1",
                    title: "Audio Foundation",
                    priority: "high",
                    completed: false,
                    notes: "Core audio playback using OpenAL Soft or miniaudio",
                    children: [
                        { id: "a1a", title: "Audio library integration (OpenAL Soft or miniaudio)", completed: false, children: [] },
                        { id: "a1b", title: "Audio device enumeration", completed: false, children: [] },
                        { id: "a1c", title: "WAV file loading", completed: false, children: [] },
                        { id: "a1d", title: "OGG Vorbis loading (stb_vorbis)", completed: false, children: [] },
                        { id: "a1e", title: "AudioClip asset type", completed: false, children: [] },
                        { id: "a1f", title: "AudioSourceComponent", completed: false, children: [] },
                        { id: "a1g", title: "Play/Stop/Pause controls", completed: false, children: [] },
                        { id: "a1h", title: "Volume and pitch control", completed: false, children: [] },
                        { id: "a1i", title: "Looping support", completed: false, children: [] },
                        { id: "a1j", title: "One-shot sound playback (fire and forget)", completed: false, children: [] }
                    ]
                },
                {
                    id: "a2",
                    title: "Spatial Audio (2D)",
                    priority: "medium",
                    completed: false,
                    notes: "Position-based audio for immersion",
                    children: [
                        { id: "a2a", title: "AudioListenerComponent", completed: false, children: [] },
                        { id: "a2b", title: "Distance attenuation models", completed: false, children: [] },
                        { id: "a2c", title: "Stereo panning based on position", completed: false, children: [] },
                        { id: "a2d", title: "Min/max distance settings", completed: false, children: [] },
                        { id: "a2e", title: "Rolloff curves", completed: false, children: [] }
                    ]
                },
                {
                    id: "a3",
                    title: "Audio Mixing",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "a3a", title: "Audio bus/channel system", completed: false, children: [] },
                        { id: "a3b", title: "Master volume", completed: false, children: [] },
                        { id: "a3c", title: "Category volumes (Music, SFX, Voice, Ambient)", completed: false, children: [] },
                        { id: "a3d", title: "Bus hierarchy (SFX -> Master)", completed: false, children: [] },
                        { id: "a3e", title: "Mute/solo per bus", completed: false, children: [] },
                        { id: "a3f", title: "Audio ducking (lower music during voice)", completed: false, children: [] }
                    ]
                },
                {
                    id: "a4",
                    title: "Music System",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "a4a", title: "Streaming playback for long tracks", completed: false, children: [] },
                        { id: "a4b", title: "Crossfade between tracks", completed: false, children: [] },
                        { id: "a4c", title: "Playlist support", completed: false, children: [] },
                        { id: "a4d", title: "Music layers (add/remove instrument tracks)", completed: false, children: [] },
                        { id: "a4e", title: "Beat sync (for rhythm games)", completed: false, children: [] }
                    ]
                },
                {
                    id: "a5",
                    title: "Audio Pooling & Optimization",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "a5a", title: "Sound instance pooling", completed: false, children: [] },
                        { id: "a5b", title: "Maximum simultaneous sounds limit", completed: false, children: [] },
                        { id: "a5c", title: "Priority-based sound culling", completed: false, children: [] },
                        { id: "a5d", title: "Sound instance stealing", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // EDITOR TOOLS
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "editor",
            title: "Editor Tools",
            category: "editor",
            priority: "high",
            completed: false,
            children: [
                {
                    id: "e1",
                    title: "Hierarchy Window",
                    priority: "high",
                    completed: true,
                    notes: "Entity tree view with parent-child relationships",
                    children: [
                        { id: "e1a", title: "Tree view rendering", completed: true, children: [] },
                        { id: "e1b", title: "Drag-drop reparenting", completed: true, children: [] },
                        { id: "e1c", title: "Context menu (create, delete, duplicate)", completed: true, children: [] },
                        { id: "e1d", title: "Multi-selection", completed: false, children: [] },
                        { id: "e1e", title: "Search/filter entities", completed: false, children: [] },
                        { id: "e1f", title: "Entity visibility toggle", completed: false, children: [] },
                        { id: "e1g", title: "Entity lock toggle", completed: false, children: [] }
                    ]
                },
                {
                    id: "e2",
                    title: "Inspector Window",
                    priority: "high",
                    completed: true,
                    notes: "Component editing with type-aware UI widgets",
                    children: [
                        { id: "e2a", title: "Component list rendering", completed: true, children: [] },
                        { id: "e2b", title: "Add component dropdown", completed: true, children: [] },
                        { id: "e2c", title: "Remove component", completed: true, children: [] },
                        { id: "e2d", title: "Auto UI for common types (int, float, vec2, string)", completed: true, children: [] },
                        { id: "e2e", title: "Color picker widget", completed: false, children: [] },
                        { id: "e2f", title: "Asset reference picker (textures, sounds)", completed: false, children: [] },
                        { id: "e2g", title: "Entity reference picker", completed: false, children: [] },
                        { id: "e2h", title: "Array/list editing", completed: false, children: [] },
                        { id: "e2i", title: "Collapsible component headers", completed: false, children: [] },
                        { id: "e2j", title: "Reset to default button", completed: false, children: [] },
                        { id: "e2k", title: "Copy/paste component values", completed: false, children: [] }
                    ]
                },
                {
                    id: "e3",
                    title: "Asset Browser",
                    priority: "medium",
                    completed: true,
                    notes: "File tree browsing with breadcrumb navigation",
                    children: [
                        { id: "e3a", title: "Directory tree", completed: true, children: [] },
                        { id: "e3b", title: "File grid view", completed: true, children: [] },
                        { id: "e3c", title: "Breadcrumb navigation", completed: true, children: [] },
                        { id: "e3d", title: "Thumbnail preview", completed: false, children: [] },
                        { id: "e3e", title: "Search/filter", completed: false, children: [] },
                        { id: "e3f", title: "Drag-drop to inspector/viewport", completed: false, children: [] },
                        { id: "e3g", title: "Create new asset (right-click)", completed: false, children: [] },
                        { id: "e3h", title: "Rename/delete assets", completed: false, children: [] },
                        { id: "e3i", title: "Asset import settings", completed: false, children: [] }
                    ]
                },
                {
                    id: "e4",
                    title: "Scene Serialization",
                    priority: "high",
                    completed: true,
                    children: [
                        { id: "e4a", title: "Save world to JSON", completed: true, children: [] },
                        { id: "e4b", title: "Load world from JSON", completed: true, children: [] },
                        { id: "e4c", title: "Dirty flag tracking", completed: false, children: [] },
                        { id: "e4d", title: "Auto-save", completed: false, children: [] },
                        { id: "e4e", title: "Recent files list", completed: false, children: [] },
                        { id: "e4f", title: "Binary format option (faster loads)", completed: false, children: [] }
                    ]
                },
                {
                    id: "e5",
                    title: "Undo/Redo System",
                    priority: "high",
                    completed: false,
                    notes: "Essential for usable editor",
                    children: [
                        { id: "e5a", title: "Command pattern implementation", completed: false, children: [] },
                        { id: "e5b", title: "History stack with limit", completed: false, children: [] },
                        { id: "e5c", title: "Ctrl+Z / Ctrl+Y shortcuts", completed: false, children: [] },
                        { id: "e5d", title: "Undo for property changes", completed: false, children: [] },
                        { id: "e5e", title: "Undo for entity create/delete", completed: false, children: [] },
                        { id: "e5f", title: "Undo for component add/remove", completed: false, children: [] },
                        { id: "e5g", title: "Undo for hierarchy changes", completed: false, children: [] },
                        { id: "e5h", title: "Group operations (drag multiple entities)", completed: false, children: [] }
                    ]
                },
                {
                    id: "e6",
                    title: "Gizmos (Transform Tools)",
                    priority: "high",
                    completed: false,
                    notes: "Visual manipulation handles",
                    children: [
                        { id: "e6a", title: "Translation gizmo (arrows)", completed: false, children: [] },
                        { id: "e6b", title: "Rotation gizmo (circle)", completed: false, children: [] },
                        { id: "e6c", title: "Scale gizmo (boxes)", completed: false, children: [] },
                        { id: "e6d", title: "Gizmo mode toggle (W/E/R keys)", completed: false, children: [] },
                        { id: "e6e", title: "Local vs World space toggle", completed: false, children: [] },
                        { id: "e6f", title: "Snap to grid", completed: false, children: [] },
                        { id: "e6g", title: "Snap angle increments", completed: false, children: [] },
                        { id: "e6h", title: "Multi-selection transform", completed: false, children: [] },
                        { id: "e6i", title: "Pivot point options (center, origin)", completed: false, children: [] }
                    ]
                },
                {
                    id: "e7",
                    title: "Viewport Controls",
                    priority: "high",
                    completed: false,
                    children: [
                        { id: "e7a", title: "Pan (middle mouse)", completed: true, children: [] },
                        { id: "e7b", title: "Zoom (scroll wheel)", completed: true, children: [] },
                        { id: "e7c", title: "Focus on selection (F key)", completed: false, children: [] },
                        { id: "e7d", title: "Frame all (Home key)", completed: false, children: [] },
                        { id: "e7e", title: "Grid overlay", completed: false, children: [] },
                        { id: "e7f", title: "Grid snapping", completed: false, children: [] },
                        { id: "e7g", title: "Ruler/measurements", completed: false, children: [] }
                    ]
                },
                {
                    id: "e8",
                    title: "Play Mode",
                    priority: "high",
                    completed: true,
                    children: [
                        { id: "e8a", title: "Play/Stop buttons", completed: true, children: [] },
                        { id: "e8b", title: "State save on play", completed: true, children: [] },
                        { id: "e8c", title: "State restore on stop", completed: true, children: [] },
                        { id: "e8d", title: "Pause button", completed: false, children: [] },
                        { id: "e8e", title: "Step frame button", completed: false, children: [] },
                        { id: "e8f", title: "Time scale slider", completed: false, children: [] },
                        { id: "e8g", title: "Keep changes dialog (on stop)", completed: false, children: [] }
                    ]
                },
                {
                    id: "e9",
                    title: "Prefab System",
                    priority: "high",
                    completed: false,
                    notes: "Reusable entity templates - huge productivity boost",
                    children: [
                        { id: "e9a", title: "Prefab asset type", completed: false, children: [] },
                        { id: "e9b", title: "Create prefab from entity", completed: false, children: [] },
                        { id: "e9c", title: "Instantiate prefab", completed: false, children: [] },
                        { id: "e9d", title: "Prefab instance link", completed: false, children: [] },
                        { id: "e9e", title: "Override tracking (instance vs prefab)", completed: false, children: [] },
                        { id: "e9f", title: "Apply overrides to prefab", completed: false, children: [] },
                        { id: "e9g", title: "Revert overrides", completed: false, children: [] },
                        { id: "e9h", title: "Nested prefabs", completed: false, children: [] },
                        { id: "e9i", title: "Prefab variants", completed: false, children: [] }
                    ]
                },
                {
                    id: "e10",
                    title: "Console/Log Window",
                    priority: "medium",
                    completed: true,
                    children: [
                        { id: "e10a", title: "Log message display", completed: true, children: [] },
                        { id: "e10b", title: "Log levels (info, warning, error)", completed: true, children: [] },
                        { id: "e10c", title: "Filter by level", completed: false, children: [] },
                        { id: "e10d", title: "Search logs", completed: false, children: [] },
                        { id: "e10e", title: "Clear button", completed: false, children: [] },
                        { id: "e10f", title: "Click to jump to source", completed: false, children: [] },
                        { id: "e10g", title: "Copy log text", completed: false, children: [] },
                        { id: "e10h", title: "Command input (future)", completed: false, children: [] }
                    ]
                },
                {
                    id: "e11",
                    title: "Hot Reload",
                    priority: "medium",
                    completed: false,
                    notes: "Recompile game code without restarting editor",
                    children: [
                        { id: "e11a", title: "DLL-based game code", completed: false, children: [] },
                        { id: "e11b", title: "File watcher for source changes", completed: false, children: [] },
                        { id: "e11c", title: "Trigger recompile", completed: false, children: [] },
                        { id: "e11d", title: "Unload old DLL", completed: false, children: [] },
                        { id: "e11e", title: "Load new DLL", completed: false, children: [] },
                        { id: "e11f", title: "Preserve entity state across reload", completed: false, children: [] },
                        { id: "e11g", title: "Re-register systems and components", completed: false, children: [] }
                    ]
                },
                {
                    id: "e12",
                    title: "Editor Preferences",
                    priority: "low",
                    completed: false,
                    children: [
                        { id: "e12a", title: "Theme (dark/light)", completed: false, children: [] },
                        { id: "e12b", title: "Font size", completed: false, children: [] },
                        { id: "e12c", title: "Grid settings", completed: false, children: [] },
                        { id: "e12d", title: "Snap settings", completed: false, children: [] },
                        { id: "e12e", title: "Keyboard shortcut customization", completed: false, children: [] },
                        { id: "e12f", title: "Layout save/load", completed: false, children: [] }
                    ]
                },
                {
                    id: "e13",
                    title: "Network Testing Tools",
                    priority: "high",
                    completed: false,
                    notes: "Essential for multiplayer development",
                    children: [
                        { id: "e13a", title: "Launch multiple instances", completed: false, children: [] },
                        { id: "e13b", title: "Simulate latency slider", completed: false, children: [] },
                        { id: "e13c", title: "Simulate packet loss slider", completed: false, children: [] },
                        { id: "e13d", title: "Network stats overlay", completed: false, children: [] },
                        { id: "e13e", title: "Entity ownership visualizer", completed: false, children: [] },
                        { id: "e13f", title: "Replication inspector", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // ECS CORE
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "ecs",
            title: "ECS Core",
            category: "rendering",
            priority: "high",
            completed: false,
            notes: "Entity Component System improvements",
            children: [
                {
                    id: "ecs1",
                    title: "Core ECS",
                    priority: "high",
                    completed: true,
                    children: [
                        { id: "ecs1a", title: "Entity creation/destruction", completed: true, children: [] },
                        { id: "ecs1b", title: "Component add/remove", completed: true, children: [] },
                        { id: "ecs1c", title: "Archetype storage", completed: true, children: [] },
                        { id: "ecs1d", title: "System registration", completed: true, children: [] },
                        { id: "ecs1e", title: "ForEach queries", completed: true, children: [] }
                    ]
                },
                {
                    id: "ecs2",
                    title: "ECS Enhancements",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "ecs2a", title: "Entity enable/disable", completed: false, children: [] },
                        { id: "ecs2b", title: "Component enable/disable", completed: false, children: [] },
                        { id: "ecs2c", title: "Deferred entity operations (during iteration)", completed: false, children: [] },
                        { id: "ecs2d", title: "Entity cloning", completed: false, children: [] },
                        { id: "ecs2e", title: "Singleton components (global data)", completed: false, children: [] },
                        { id: "ecs2f", title: "System ordering/dependencies", completed: false, children: [] },
                        { id: "ecs2g", title: "System groups", completed: false, children: [] }
                    ]
                },
                {
                    id: "ecs3",
                    title: "Query System",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "ecs3a", title: "Cached queries (avoid recomputing)", completed: false, children: [] },
                        { id: "ecs3b", title: "Optional component queries", completed: false, children: [] },
                        { id: "ecs3c", title: "Exclude component queries", completed: false, children: [] },
                        { id: "ecs3d", title: "Query change detection (only process changed)", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // SCRIPTING & GAMEPLAY
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "scripting",
            title: "Scripting & Gameplay",
            category: "editor",
            priority: "medium",
            completed: false,
            notes: "Make it easy to write game logic",
            children: [
                {
                    id: "s1",
                    title: "Gameplay Helpers",
                    priority: "high",
                    completed: false,
                    children: [
                        { id: "s1a", title: "Timer system (delay, repeat)", completed: false, children: [] },
                        { id: "s1b", title: "Tweening library (ease in/out, lerp)", completed: false, children: [] },
                        { id: "s1c", title: "Coroutine-like sequencing", completed: false, children: [] },
                        { id: "s1d", title: "State machine base class", completed: false, children: [] },
                        { id: "s1e", title: "Object pooling utilities", completed: false, children: [] },
                        { id: "s1f", title: "Random number utilities", completed: false, children: [] }
                    ]
                },
                {
                    id: "s2",
                    title: "Scene Management",
                    priority: "high",
                    completed: false,
                    children: [
                        { id: "s2a", title: "Scene asset type", completed: false, children: [] },
                        { id: "s2b", title: "Load scene (replace current)", completed: false, children: [] },
                        { id: "s2c", title: "Load scene additive", completed: false, children: [] },
                        { id: "s2d", title: "Unload scene", completed: false, children: [] },
                        { id: "s2e", title: "Scene transition effects", completed: false, children: [] },
                        { id: "s2f", title: "Persistent entities (DontDestroyOnLoad)", completed: false, children: [] }
                    ]
                },
                {
                    id: "s3",
                    title: "Lua Scripting (Future)",
                    priority: "low",
                    completed: false,
                    notes: "Optional scripting for rapid iteration",
                    children: [
                        { id: "s3a", title: "Lua integration (sol2 or similar)", completed: false, children: [] },
                        { id: "s3b", title: "Bind ECS to Lua", completed: false, children: [] },
                        { id: "s3c", title: "Bind Input to Lua", completed: false, children: [] },
                        { id: "s3d", title: "Script component", completed: false, children: [] },
                        { id: "s3e", title: "Hot reload scripts", completed: false, children: [] },
                        { id: "s3f", title: "Script editor integration", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // BUILD & DEPLOYMENT
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "build",
            title: "Build & Deployment",
            category: "editor",
            priority: "medium",
            completed: false,
            children: [
                {
                    id: "b1",
                    title: "Project System",
                    priority: "high",
                    completed: false,
                    children: [
                        { id: "b1a", title: "Project file format (.zues)", completed: true, children: [] },
                        { id: "b1b", title: "Project settings storage", completed: false, children: [] },
                        { id: "b1c", title: "New project wizard", completed: false, children: [] },
                        { id: "b1d", title: "Open existing project", completed: false, children: [] },
                        { id: "b1e", title: "Recent projects list", completed: false, children: [] },
                        { id: "b1f", title: "Project templates", completed: false, children: [] }
                    ]
                },
                {
                    id: "b2",
                    title: "Build System",
                    priority: "high",
                    completed: false,
                    children: [
                        { id: "b2a", title: "Build configuration (Debug/Release)", completed: false, children: [] },
                        { id: "b2b", title: "One-click build button", completed: false, children: [] },
                        { id: "b2c", title: "Asset packing/bundling", completed: false, children: [] },
                        { id: "b2d", title: "Executable generation", completed: false, children: [] },
                        { id: "b2e", title: "Build output directory", completed: false, children: [] },
                        { id: "b2f", title: "Build log window", completed: false, children: [] }
                    ]
                },
                {
                    id: "b3",
                    title: "Platform Targets",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "b3a", title: "Windows build", completed: false, children: [] },
                        { id: "b3b", title: "Linux build", completed: false, children: [] },
                        { id: "b3c", title: "macOS build", completed: false, children: [] },
                        { id: "b3d", title: "Web/Emscripten (future)", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // DOCUMENTATION & EXAMPLES
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "docs",
            title: "Documentation & Examples",
            category: "editor",
            priority: "medium",
            completed: false,
            children: [
                {
                    id: "d1",
                    title: "API Documentation",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "d1a", title: "Doxygen setup", completed: false, children: [] },
                        { id: "d1b", title: "Document public API headers", completed: false, children: [] },
                        { id: "d1c", title: "Generate HTML docs", completed: false, children: [] }
                    ]
                },
                {
                    id: "d2",
                    title: "Example Projects",
                    priority: "high",
                    completed: false,
                    children: [
                        { id: "d2a", title: "Pong (basic networking)", completed: false, children: [] },
                        { id: "d2b", title: "Platformer (physics, animation)", completed: false, children: [] },
                        { id: "d2c", title: "Top-down shooter (networking, prediction)", completed: false, children: [] },
                        { id: "d2d", title: "Chat room (pure networking)", completed: false, children: [] }
                    ]
                },
                {
                    id: "d3",
                    title: "Tutorials",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "d3a", title: "Getting started guide", completed: false, children: [] },
                        { id: "d3b", title: "Your first multiplayer game", completed: false, children: [] },
                        { id: "d3c", title: "Understanding client prediction", completed: false, children: [] },
                        { id: "d3d", title: "Custom components guide", completed: false, children: [] }
                    ]
                }
            ]
        },

        // ═══════════════════════════════════════════════════════════════════
        // QUALITY OF LIFE
        // ═══════════════════════════════════════════════════════════════════
        {
            id: "qol",
            title: "Quality of Life",
            category: "editor",
            priority: "medium",
            completed: false,
            notes: "Small features that make development smoother",
            children: [
                {
                    id: "q1",
                    title: "Debug Visualization",
                    priority: "high",
                    completed: false,
                    children: [
                        { id: "q1a", title: "Debug.DrawLine (in-game)", completed: false, children: [] },
                        { id: "q1b", title: "Debug.DrawCircle", completed: false, children: [] },
                        { id: "q1c", title: "Debug.DrawRect", completed: false, children: [] },
                        { id: "q1d", title: "Debug.DrawText (world-space labels)", completed: false, children: [] },
                        { id: "q1e", title: "Debug draw duration parameter", completed: false, children: [] },
                        { id: "q1f", title: "Debug draw depth testing option", completed: false, children: [] },
                        { id: "q1g", title: "Toggle debug rendering in editor", completed: false, children: [] }
                    ]
                },
                {
                    id: "q2",
                    title: "Performance Profiling",
                    priority: "medium",
                    completed: false,
                    children: [
                        { id: "q2a", title: "Frame time graph", completed: false, children: [] },
                        { id: "q2b", title: "System timing breakdown", completed: false, children: [] },
                        { id: "q2c", title: "Entity count display", completed: false, children: [] },
                        { id: "q2d", title: "Draw call count", completed: false, children: [] },
                        { id: "q2e", title: "Memory usage tracking", completed: false, children: [] },
                        { id: "q2f", title: "Profiler timeline view", completed: false, children: [] }
                    ]
                },
                {
                    id: "q3",
                    title: "Developer Experience",
                    priority: "high",
                    completed: false,
                    children: [
                        { id: "q3a", title: "Error messages with source location", completed: false, children: [] },
                        { id: "q3b", title: "Assert with custom messages", completed: false, children: [] },
                        { id: "q3c", title: "Configurable logging levels", completed: false, children: [] },
                        { id: "q3d", title: "Log to file option", completed: false, children: [] },
                        { id: "q3e", title: "Crash handler with stack trace", completed: false, children: [] },
                        { id: "q3f", title: "Helpful error messages for common mistakes", completed: false, children: [] }
                    ]
                }
            ]
        }
    ],

    wikiPages: [
        {
            id: "overview",
            title: "Overview",
            content: `<h1>ZuesEngine - Overview</h1>
<p>ZuesEngine is a high-performance 2D game engine built from scratch in C++ with a <strong>primary focus on multiplayer game development</strong>. The engine is designed to make networked gameplay accessible while maintaining excellent performance through modern architectural patterns.</p>

<h2>Core Philosophy</h2>
<p>The engine is built around four key principles:</p>
<ul>
<li><strong>Multiplayer First</strong> - Networking is integrated at the core level, not bolted on as an afterthought. Entity replication, client prediction, and server reconciliation are first-class features.</li>
<li><strong>High Performance</strong> - Archetype-based ECS ensures cache-friendly data layouts. Batch rendering minimizes draw calls. Everything is optimized for 2D games running at 60+ FPS.</li>
<li><strong>Developer Experience</strong> - Clean APIs, comprehensive editor tools, hot-reload support, and visual debugging make iteration fast.</li>
<li><strong>2D Focused</strong> - By targeting only 2D games, we avoid the complexity of 3D engines and provide purpose-built solutions for 2D challenges.</li>
</ul>

<h2>Technical Stack</h2>
<ul>
<li><strong>Language:</strong> C++17/23</li>
<li><strong>Graphics:</strong> OpenGL 3.3+</li>
<li><strong>Physics:</strong> Box2D v3 (latest)</li>
<li><strong>Networking:</strong> ENet (UDP-based)</li>
<li><strong>UI/Editor:</strong> Dear ImGui</li>
<li><strong>Serialization:</strong> nlohmann/json</li>
<li><strong>Build System:</strong> CMake</li>
</ul>

<h2>Key Features</h2>
<h3>Entity Component System (ECS)</h3>
<p>Archetype-based ECS with automatic component migration, efficient ForEach queries, and full serialization support.</p>

<h3>Rendering Pipeline</h3>
<p>Custom batch renderer supporting 20,000 quads per draw call with 32 simultaneous texture slots.</p>

<h3>Physics Simulation</h3>
<p>Full Box2D v3 integration with rigidbodies, colliders, friction, restitution, and triggers.</p>

<h3>Editor Tools</h3>
<p>ImGui-based editor featuring hierarchy, inspector, asset browser, and play mode.</p>

<h3>Networking (In Development)</h3>
<p>Entity replication, RPC system, client prediction, server reconciliation, and entity interpolation.</p>

<h2>Development Status</h2>
<p>ZuesEngine is in active development. Core rendering, ECS, physics, and editor tools are functional. Networking features are the current priority. See the Tasks view for detailed progress.</p>`
        },
        {
            id: "networking-guide",
            title: "Networking Guide",
            content: `<h1>Networking System Guide</h1>

<h2>Architecture Overview</h2>
<p>ZuesEngine uses a <strong>server-authoritative</strong> networking model with client-side prediction for responsive gameplay.</p>

<h3>Key Concepts</h3>
<ul>
<li><strong>Server Authority</strong> - The server is the source of truth for game state</li>
<li><strong>Client Prediction</strong> - Clients simulate locally for responsiveness</li>
<li><strong>Entity Replication</strong> - Server syncs entity state to clients</li>
<li><strong>RPCs</strong> - Remote procedure calls for actions</li>
<li><strong>Interpolation</strong> - Smooth movement of remote entities</li>
</ul>

<h2>Basic Setup</h2>
<pre><code>// Initialize networking
Network::Init();

// Host a game (server)
Network::Host("0.0.0.0", 7777, 32); // IP, port, max players

// Join a game (client)
Network::Connect("192.168.1.100", 7777);

// In update loop
Network::Update();</code></pre>

<h2>Entity Replication (Planned)</h2>
<pre><code>// Mark an entity for network replication
world->AddComponent(player, NetworkIDComponent{});
world->AddComponent(player, ReplicatedComponent{
    .replicateTransform = true,
    .replicateVelocity = true,
    .ownerClientID = clientID
});</code></pre>

<h2>RPC System (Planned)</h2>
<pre><code>// Define an RPC
ZUES_RPC(ServerRPC, FireWeapon, Vec2 direction) {
    // Only runs on server
    SpawnBullet(GetOwnerEntity(), direction);
}

// Call the RPC (from client)
FireWeapon_ServerRPC(direction);</code></pre>

<h2>Client Prediction (Planned)</h2>
<pre><code>// In player controller
void ProcessInput(InputState input) {
    // Apply locally immediately
    ApplyMovement(input);

    // Send to server
    SendInput_ServerRPC(input, timestamp);
}

// Server processes and sends corrections
// Client reconciles if needed</code></pre>

<h2>Best Practices</h2>
<ul>
<li>Keep networked data minimal - only sync what's needed</li>
<li>Use RPCs for discrete actions (fire, jump, interact)</li>
<li>Use replication for continuous state (position, health)</li>
<li>Test with simulated latency early and often</li>
<li>Profile bandwidth usage during development</li>
</ul>`
        },
        {
            id: "ecs",
            title: "ECS Guide",
            content: `<h1>Entity Component System Guide</h1>

<h2>Overview</h2>
<p>ZuesEngine uses an archetype-based ECS for optimal cache performance and clean architecture.</p>

<h2>Creating Entities</h2>
<pre><code>World* world = Engine::Core::GetCurrentWorld();

// Create named entity
EntityID player = world->CreateEntity("Player");

// Add components
world->AddComponent(player, TransformComponent{
    .worldPosition = {0, 5, 0}
});

world->AddComponent(player, SpriteComponent{
    .spriteName = "player_idle",
    .size = {1, 1.5}
});</code></pre>

<h2>Creating Systems</h2>
<pre><code>class MovementSystem : public SystemBase<
    TransformComponent*,
    VelocityComponent*>
{
public:
    MovementSystem() {
        role = SystemRole::Game;
    }

    void Update(float dt,
                TransformComponent* transform,
                VelocityComponent* vel) override {
        transform->worldPosition.x += vel->x * dt;
        transform->worldPosition.y += vel->y * dt;
    }
};</code></pre>

<h2>System Roles</h2>
<ul>
<li><strong>SystemRole::Shared</strong> - Runs always (rendering)</li>
<li><strong>SystemRole::Editor</strong> - Only in editor (gizmos)</li>
<li><strong>SystemRole::Game</strong> - Only in play mode (physics, AI)</li>
</ul>

<h2>Best Practices</h2>
<ul>
<li>Keep components small and focused</li>
<li>No logic in components - data only</li>
<li>Use tag components for categorization</li>
<li>Register systems in execution order</li>
</ul>`
        },
        {
            id: "rendering",
            title: "Rendering Guide",
            content: `<h1>Rendering System Guide</h1>

<h2>Sprite Rendering</h2>
<pre><code>// Create sprite entity
EntityID sprite = world->CreateEntity("MySprite");

world->AddComponent(sprite, TransformComponent{
    .worldPosition = {0, 0, 0}
});

world->AddComponent(sprite, SpriteComponent{
    .spriteName = "texture_name",
    .size = {2, 2},
    .color = {1, 1, 1, 1},
    .layer = 0,
    .sortOrder = 0
});</code></pre>

<h2>Camera Setup</h2>
<pre><code>EntityID camera = world->CreateEntity("MainCamera");

world->AddComponent(camera, TransformComponent{});
world->AddComponent(camera, CameraComponent{
    .zoom = 1.0f,
    .halfHeight = 10.0f,  // Shows 20 units vertically
    .backgroundColor = {0.1f, 0.1f, 0.1f, 1.0f},
    .isActive = true
});
world->AddComponent(camera, MainCameraTag{});</code></pre>

<h2>Text Rendering</h2>
<pre><code>uint32_t fontID = Engine::TextRenderer::LoadFont("font.ttf", 48);

world->AddComponent(entity, TextComponent{
    .text = "Hello World!",
    .fontID = fontID,
    .color = {1, 1, 1, 1},
    .scale = 1.0f
});</code></pre>

<h2>Layering</h2>
<p>Sprites are sorted by layer (low to high), then by sortOrder within the same layer.</p>
<pre><code>background.layer = 0;
player.layer = 5;
ui.layer = 100;</code></pre>`
        },
        {
            id: "physics",
            title: "Physics Guide",
            content: `<h1>Physics System Guide</h1>

<h2>Creating Physics Bodies</h2>
<pre><code>// Dynamic body (affected by forces)
world->AddComponent(entity, RigidbodyComponent{
    .bodyType = BodyType::Dynamic,
    .mass = 1.0f,
    .gravityScale = 1.0f,
    .fixedRotation = false
});

// Add collider
world->AddComponent(entity, BoxColliderComponent{
    .size = {1.0f, 1.0f},
    .density = 1.0f,
    .friction = 0.3f,
    .restitution = 0.5f,
    .isTrigger = false
});</code></pre>

<h2>Body Types</h2>
<ul>
<li><strong>Static</strong> - Immovable (walls, floors)</li>
<li><strong>Kinematic</strong> - Moved by code only (platforms)</li>
<li><strong>Dynamic</strong> - Fully simulated (players, objects)</li>
</ul>

<h2>Material Properties</h2>
<ul>
<li><strong>Friction</strong> - 0 = ice, 1 = rubber</li>
<li><strong>Restitution</strong> - 0 = no bounce, 1 = perfect bounce</li>
<li><strong>Density</strong> - Mass per area</li>
</ul>

<h2>Collision Events (Planned)</h2>
<pre><code>world->AddComponent(entity, CollisionCallbackComponent{
    .onCollisionEnter = [](EntityID other, Vec2 point) {
        // Handle collision
    }
});</code></pre>`
        },
        {
            id: "editor",
            title: "Editor Guide",
            content: `<h1>Editor Workflow</h1>

<h2>Viewport Controls</h2>
<ul>
<li><strong>Pan:</strong> Middle mouse drag</li>
<li><strong>Zoom:</strong> Mouse scroll wheel</li>
<li><strong>Select:</strong> Left click on entity</li>
</ul>

<h2>Hierarchy Window</h2>
<p>Shows all entities in a tree structure. Drag entities to reparent them. Right-click for context menu.</p>

<h2>Inspector Window</h2>
<p>Shows components of selected entity. Modify values directly. Add/remove components.</p>

<h2>Asset Browser</h2>
<p>Navigate project files. Double-click to open. Drag assets to inspector.</p>

<h2>Play Mode</h2>
<ol>
<li>Click Play button</li>
<li>World state is automatically saved</li>
<li>Game systems run (physics, gameplay)</li>
<li>Click Stop to restore original state</li>
</ol>

<h2>Scene Save/Load</h2>
<pre><code>Engine::Core::SaveWorld("Worlds/level1.json");
Engine::Core::LoadWorld("Worlds/level1.json");</code></pre>`
        },
        {
            id: "getting-started",
            title: "Getting Started",
            content: `<h1>Getting Started with ZuesEngine</h1>

<h2>Prerequisites</h2>
<ul>
<li>C++17 compatible compiler (MSVC, GCC, Clang)</li>
<li>CMake 3.20+</li>
<li>OpenGL 3.3+ capable GPU</li>
</ul>

<h2>Building</h2>
<pre><code># Clone repository
git clone https://github.com/your/ZuesEngine.git

# Configure
cmake -B build -G "Visual Studio 17 2022"

# Build
cmake --build build --config Release</code></pre>

<h2>Project Structure</h2>
<pre><code>ZuesEngine/
├── Engine/        # Core engine library
├── Editor/        # Development editor
├── MyGameProject/ # Example game
└── ProjectTracker/# This documentation</code></pre>

<h2>First Steps</h2>
<ol>
<li>Open the Editor</li>
<li>Create entities in Hierarchy</li>
<li>Add components in Inspector</li>
<li>Press Play to test</li>
<li>Save your world</li>
</ol>

<h2>Next Steps</h2>
<ul>
<li>Read the ECS Guide to understand entities</li>
<li>Check Rendering Guide for sprites</li>
<li>Explore Physics Guide for collisions</li>
<li>Study Networking Guide for multiplayer</li>
</ul>`
        }
    ]
};
