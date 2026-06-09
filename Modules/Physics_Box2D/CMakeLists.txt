# Box2D 3.0 physics backend. Implements IPhysics_v1; consumes RigidBody +
# BoxCollider + CircleCollider components that live in Core. Body/shape
# lifecycles are driven by the PrePhysics system (creates b2 bodies for
# new components) and the PostPhysics system (writes back transforms,
# drains contact events, fires hooks).
zues_fetch_box2d()

zues_add_module(zues_physics_box2d
    SOURCES src/module.cpp
    LINKS   box2d)
