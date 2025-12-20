// Extended Wiki Documentation
// This file adds comprehensive documentation pages to window.defaultData.wikiPages
// Load this after data.js

if (window.defaultData && window.defaultData.wikiPages) {
    // Find and replace/extend existing pages or add new ones

    const additionalPages = [
        {
            id: "rendering-advanced",
            title: "Rendering (Advanced)",
            content: `<h1>Rendering System - Advanced Topics</h1>

<h2>Batch Rendering Architecture</h2>
<p>The renderer uses a sophisticated batching system to minimize draw calls and maximize throughput.</p>

<h3>Vertex Buffer Layout</h3>
<pre><code>struct QuadVertex {
    Vec3 position;     // 12 bytes
    Vec2 texCoord;     // 8 bytes
    float texIndex;    // 4 bytes (which texture slot)
    Vec4 color;        // 16 bytes
};  // Total: 40 bytes per vertex, 160 bytes per quad

// Max batch: 20,000 quads = 80,000 vertices = 6.4 MB</code></pre>

<h3>Texture Slot Management</h3>
<pre><code>// 32 texture slots available
// Slot 0: White texture (for colored quads)
// Slots 1-31: User textures

// When batch is full (20k quads or 32 textures):
void FlushBatch() {
    // 1. Upload vertex data to GPU
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexCount * sizeof(Vertex), vertices);

    // 2. Bind all active textures
    for (int i = 0; i < textureSlotIndex; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textureSlots[i]);
    }

    // 3. Draw everything in one call
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);

    // 4. Reset batch
    vertexCount = 0;
    indexCount = 0;
    textureSlotIndex = 1; // Keep white texture
}</code></pre>

<h2>Z-Depth and Layering</h2>
<p>The renderer supports multiple layers with sorting.</p>

<pre><code>// In SpriteComponent
int layer = 0;          // Primary sort (0 = background, higher = foreground)
int sortOrder = 0;      // Secondary sort within layer

// Rendering order:
// 1. Sort by layer (low to high)
// 2. Within layer, sort by sortOrder
// 3. Draw back to front</code></pre>

<h2>Camera System Details</h2>

<h3>Orthographic Projection</h3>
<pre><code>// Camera defines view frustum
float halfHeight = 10.0f;  // Shows 20 world units vertically
float aspectRatio = viewportWidth / viewportHeight;
float halfWidth = halfHeight * aspectRatio;

// Projection matrix
left = cameraX - halfWidth
right = cameraX + halfWidth
bottom = cameraY - halfHeight
top = cameraY + halfHeight</code></pre>

<h3>Coordinate Conversion</h3>
<pre><code>// Screen to world (for mouse input)
Vec2 ScreenToWorld(Vec2 screenPos) {
    // Normalize to [-1, 1]
    float ndcX = (screenPos.x / viewportWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenPos.y / viewportHeight) * 2.0f;

    // Apply camera transform
    Vec2 worldPos;
    worldPos.x = cameraX + ndcX * halfWidth / zoom;
    worldPos.y = cameraY + ndcY * halfHeight / zoom;
    return worldPos;
}

// World to screen (for UI overlays)
Vec2 WorldToScreen(Vec2 worldPos) {
    // Reverse of above
    float ndcX = ((worldPos.x - cameraX) * zoom / halfWidth);
    float ndcY = ((worldPos.y - cameraY) * zoom / halfHeight);

    Vec2 screenPos;
    screenPos.x = (ndcX + 1.0f) * 0.5f * viewportWidth;
    screenPos.y = (1.0f - ndcY) * 0.5f * viewportHeight;
    return screenPos;
}</code></pre>

<h2>Sprite Sheet System</h2>

<h3>UV Rectangles</h3>
<p>Sprites are defined by UV coordinates in texture space [0, 1].</p>

<pre><code>struct UVRect {
    float x, y;          // Top-left corner
    float width, height; // Size in texture space
};

// Example: 512x512 texture, sprite at pixel (128, 0) size 64x64
UVRect sprite = {
    .x = 128.0f / 512.0f,      // 0.25
    .y = 0.0f / 512.0f,         // 0.0
    .width = 64.0f / 512.0f,   // 0.125
    .height = 64.0f / 512.0f   // 0.125
};</code></pre>

<h3>Sprite Metadata Format</h3>
<pre><code>{
  "version": 1,
  "texturePath": "Assets/characters.png",
  "sprites": [
    {
      "name": "player_idle_0",
      "uvRect": {
        "x": 0.0,
        "y": 0.0,
        "width": 0.125,
        "height": 0.125
      }
    },
    {
      "name": "player_run_0",
      "uvRect": {
        "x": 0.125,
        "y": 0.0,
        "width": 0.125,
        "height": 0.125
      }
    }
  ]
}</code></pre>

<h2>Text Rendering Implementation</h2>

<h3>Font Atlas Generation</h3>
<pre><code>// STB TrueType generates bitmap atlas
1. Load TTF file
2. Rasterize characters 32-126 (ASCII printable)
3. Pack into 1024x1024 texture
4. Store character metrics:
   - Advance (horizontal spacing)
   - Bearing (offset from baseline)
   - Size (character dimensions)
   - UV coords in atlas</code></pre>

<h3>Text Layout</h3>
<pre><code>void DrawText(const std::string& text, Vec2 position) {
    float cursorX = position.x;

    for (char c : text) {
        CharMetrics& metrics = font->GetChar(c);

        // Calculate quad position
        Vec2 quadPos = {
            cursorX + metrics.bearing.x,
            position.y + metrics.bearing.y
        };

        // Draw character quad with UV from atlas
        DrawQuad(quadPos, metrics.size, 0.0f, color,
                 font->atlasTexture, metrics.uvRect);

        // Advance cursor
        cursorX += metrics.advance;
    }
}</code></pre>

<h2>Performance Optimization</h2>

<h3>Batching Best Practices</h3>
<ul>
<li>Use texture atlases to minimize texture switches</li>
<li>Sort sprites by texture before submitting to batch</li>
<li>Avoid changing render states mid-batch</li>
<li>Submit static geometry first, dynamic last</li>
<li>Use sprite layers to control draw order without sorting</li>
</ul>

<h3>Profiling</h3>
<pre><code>// Key metrics to monitor
- Draw calls per frame (goal: < 10)
- Quads per frame (20k max per batch)
- Texture swaps (goal: < 32)
- Frame time (goal: < 16.67ms for 60 FPS)</code></pre>

<h2>Shader Source</h2>

<h3>Vertex Shader</h3>
<pre><code>#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in float a_TexIndex;
layout(location = 3) in vec4 a_Color;

uniform mat4 u_ViewProjection;

out vec2 v_TexCoord;
out float v_TexIndex;
out vec4 v_Color;

void main() {
    v_TexCoord = a_TexCoord;
    v_TexIndex = a_TexIndex;
    v_Color = a_Color;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}</code></pre>

<h3>Fragment Shader</h3>
<pre><code>#version 330 core

in vec2 v_TexCoord;
in float v_TexIndex;
in vec4 v_Color;

uniform sampler2D u_Textures[32];

out vec4 FragColor;

void main() {
    int index = int(v_TexIndex);
    vec4 texColor = texture(u_Textures[index], v_TexCoord);
    FragColor = texColor * v_Color;
}</code></pre>`
        },

        {
            id: "physics-advanced",
            title: "Physics (Advanced)",
            content: `<h1>Physics System - Advanced Topics</h1>

<h2>Box2D v3 Integration Details</h2>

<h3>World Setup</h3>
<pre><code>// In PhysicsSystem constructor
b2WorldDef worldDef = b2DefaultWorldDef();
worldDef.gravity = {0.0f, -9.8f};  // Earth gravity
physicsWorldId = b2CreateWorld(&worldDef);

// Physics timestep
float fixedTimeStep = 1.0f / 60.0f;  // 60 Hz
int32_t subStepCount = 4;  // 4 sub-steps per step</code></pre>

<h3>Body Creation Flow</h3>
<pre><code>void PhysicsSystem::CreateBodies(World* world) {
    // Query entities with Transform + Rigidbody but no Box2D body yet
    world->ForEach<TransformComponent*, RigidbodyComponent*>(
        [&](EntityID entityID, TransformComponent* transform, RigidbodyComponent* rb) {

            // Skip if already has body
            if (entityBodyMap.count(entityID) > 0) return;

            // Create Box2D body
            b2BodyDef bodyDef = b2DefaultBodyDef();
            bodyDef.type = ConvertBodyType(rb->bodyType);
            bodyDef.position = {transform->worldPosition.x, transform->worldPosition.y};
            bodyDef.rotation = b2MakeRot(transform->rotation);
            bodyDef.fixedRotation = rb->fixedRotation;
            bodyDef.gravityScale = rb->gravityScale;

            b2BodyId bodyId = b2CreateBody(physicsWorldId, &bodyDef);

            // Store mapping
            entityBodyMap[entityID] = bodyId;

            // Create fixtures (colliders)
            CreateFixtures(bodyId, world, entityID);
        }
    );
}</code></pre>

<h3>Fixture Creation</h3>
<pre><code>void PhysicsSystem::CreateFixtures(b2BodyId bodyId, World* world, EntityID entityID) {
    // Box collider
    if (world->HasComponent<BoxColliderComponent>(entityID)) {
        auto& box = world->GetComponent<BoxColliderComponent>(entityID);

        b2Polygon polygon = b2MakeBox(box.size.x / 2.0f, box.size.y / 2.0f);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = box.density;
        shapeDef.friction = box.friction;
        shapeDef.restitution = box.restitution;
        shapeDef.isSensor = box.isTrigger;

        b2CreatePolygonShape(bodyId, &shapeDef, &polygon);
    }

    // Circle collider
    if (world->HasComponent<CircleColliderComponent>(entityID)) {
        auto& circle = world->GetComponent<CircleColliderComponent>(entityID);

        b2Circle circleShape;
        circleShape.center = {circle.offset.x, circle.offset.y};
        circleShape.radius = circle.radius;

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.density = circle.density;
        shapeDef.friction = circle.friction;
        shapeDef.restitution = circle.restitution;
        shapeDef.isSensor = circle.isTrigger;

        b2CreateCircleShape(bodyId, &shapeDef, &circleShape);
    }
}</code></pre>

<h2>Fixed Timestep Simulation</h2>

<pre><code>void PhysicsSystem::Run(World* world, float deltaTime) {
    // Accumulate time
    timeAccumulator += deltaTime;

    // Step physics in fixed increments
    while (timeAccumulator >= fixedTimeStep) {
        StepPhysics(fixedTimeStep);
        timeAccumulator -= fixedTimeStep;
    }

    // Sync transforms after all steps
    SyncTransforms(world);
}</code></pre>

<h3>Why Fixed Timestep?</h3>
<ul>
<li>Physics simulation is deterministic with consistent timestep</li>
<li>Prevents tunneling at low framerates</li>
<li>Makes networked physics easier to sync</li>
<li>Standard practice in game physics</li>
</ul>

<h2>Transform Synchronization</h2>

<pre><code>void PhysicsSystem::SyncTransforms(World* world) {
    for (auto& [entityID, bodyId] : entityBodyMap) {
        // Get physics body transform
        b2Vec2 position = b2Body_GetPosition(bodyId);
        b2Rot rotation = b2Body_GetRotation(bodyId);

        // Update ECS transform
        auto& transform = world->GetComponent<TransformComponent>(entityID);
        transform.worldPosition.x = position.x;
        transform.worldPosition.y = position.y;
        transform.rotation = b2Rot_GetAngle(rotation);
    }
}</code></pre>

<h2>Material Properties</h2>

<h3>Friction</h3>
<p>How rough the surface is (0 = ice, 1 = rubber).</p>
<pre><code>// Combined friction (geometric mean)
friction = sqrt(friction1 * friction2)

// Example:
// Ice (0.1) on rubber (0.9) = sqrt(0.1 * 0.9) = 0.3</code></pre>

<h3>Restitution (Bounciness)</h3>
<p>How much energy is retained after collision (0 = no bounce, 1 = perfect bounce).</p>
<pre><code>// Combined restitution (maximum)
restitution = max(restitution1, restitution2)

// Example:
// Ball (0.8) on ground (0.0) = max(0.8, 0.0) = 0.8 (ball bounces)</code></pre>

<h3>Density</h3>
<p>Mass per unit area. Total mass = density × area.</p>
<pre><code>// Box collider (2m × 2m, density 1.0)
area = 2 * 2 = 4 m²
mass = 1.0 * 4 = 4 kg

// Circle collider (radius 1m, density 1.0)
area = π * 1² = 3.14 m²
mass = 1.0 * 3.14 = 3.14 kg</code></pre>

<h2>Body Types in Detail</h2>

<h3>Static Bodies</h3>
<ul>
<li>Zero mass, infinite inertia</li>
<li>Never moves</li>
<li>Optimal for walls, floors, obstacles</li>
<li>Can collide with dynamic bodies</li>
<li>Cannot collide with other static or kinematic bodies</li>
</ul>

<h3>Kinematic Bodies</h3>
<ul>
<li>Zero mass, but can move via code</li>
<li>Not affected by forces or collisions</li>
<li>Can push dynamic bodies</li>
<li>Perfect for moving platforms, elevators</li>
</ul>

<h3>Dynamic Bodies</h3>
<ul>
<li>Has mass and inertia</li>
<li>Affected by gravity, forces, collisions</li>
<li>Most expensive to simulate</li>
<li>Use for players, enemies, projectiles, physics objects</li>
</ul>

<h2>Performance Optimization</h2>

<h3>Sleeping Bodies</h3>
<p>Box2D automatically puts inactive bodies to sleep to save CPU.</p>
<pre><code>// Body sleeps when:
// - Linear velocity < 0.01 m/s
// - Angular velocity < (2 deg/s)
// - No collisions
// - Stable for 0.5 seconds

// Sleeping bodies don't simulate until "woken" by:
// - Collision with active body
// - Applied force/impulse
// - Manual wake call</code></pre>

<h3>Broad Phase Optimization</h3>
<p>Box2D uses a dynamic AABB tree for collision detection.</p>
<pre><code>// Avoid:
// - Tiny bodies (< 0.1m)
// - Huge bodies (> 100m)
// - Long thin bodies (aspect ratio > 10:1)
// - Too many bodies in small area (> 100 per 10m²)</code></pre>

<h2>Common Pitfalls</h2>

<h3>Tunneling</h3>
<p>Fast-moving objects passing through thin walls.</p>
<pre><code>// Solutions:
// 1. Increase sub-step count
subStepCount = 8;  // Default is 4

// 2. Use continuous collision detection (CCD)
bodyDef.enableContinuous = true;

// 3. Make walls thicker
// 4. Limit maximum velocity</code></pre>

<h3>Unstable Stacking</h3>
<p>Stacked boxes jittering or collapsing.</p>
<pre><code>// Solutions:
// 1. Increase position iterations
// 2. Lower density of upper boxes
// 3. Use joint constraints instead
// 4. Weld bottom boxes together</code></pre>

<h2>Debugging</h2>

<h3>Debug Draw</h3>
<pre><code>// Draw physics shapes in editor
for (auto& [entityID, bodyId] : entityBodyMap) {
    b2Vec2 pos = b2Body_GetPosition(bodyId);

    // Draw collider outline
    if (HasComponent<BoxColliderComponent>(entityID)) {
        auto& box = GetComponent<BoxColliderComponent>(entityID);
        Renderer::DrawRect({pos.x, pos.y}, box.size, 0,
                          {0, 1, 0, 1}); // Green outline
    }
}</code></pre>

<h3>Gizmo Visualization</h3>
<p>The editor draws physics colliders automatically:</p>
<ul>
<li>Green outline: Box colliders</li>
<li>Blue outline: Circle colliders</li>
<li>Yellow outline: Selected entity</li>
<li>Handles for resizing colliders</li>
</ul>`
        }
    ];

    // Add new pages to existing wikiPages
    window.defaultData.wikiPages.push(...additionalPages);
}
