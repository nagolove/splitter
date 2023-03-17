#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_types.h"
#include "chipmunk/cpVect.h"
#include "koh_common.h"
#include "koh_destral_ecs.h"
#include "koh_logger.h"
#include "koh_object.h"
#include "koh_render.h"
#include "koh_routine.h"
#include "koh_stages.h"
#include "raylib.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

static Camera2D cam = {0};
static cpVect gravity = { 0, 9.8 * 20. };
static bool use_gravity = false;
static cpBool lastClickState = cpFalse;
static cpVect sliceStart = {0.0, 0.0};
static Font fnt = {0};
static bool is_paused = false;

#define GRABBABLE_MASK_BIT (1<<31)
static cpShapeFilter GRAB_FILTER = {
    CP_NO_GROUP, GRABBABLE_MASK_BIT, GRABBABLE_MASK_BIT
};
static cpShapeFilter NOT_GRABBABLE_FILTER = {
    CP_NO_GROUP, ~GRABBABLE_MASK_BIT, ~GRABBABLE_MASK_BIT
};

#define DENSITY (1.0/10000.0)
#define MAX_ENTITIES    256

typedef struct Stage_Splitter {
    Stage   parent;
    cpSpace *space;
    de_ecs  *r;
    de_entity polygons[MAX_ENTITIES];
    int       polygon_num;
} Stage_Splitter;

struct SliceContext {
	cpVect a, b;
	cpSpace *space;
};

struct Component_Body {
    cpBody  *b;
    cpShape *shape;
};

struct Component_Textured {
    RenderTexture2D tex;
};

static void _init(Stage_Splitter *st);
static void _shutdown(Stage_Splitter *st);

static de_cp_type comp_body = {
    .cp_id = 1,
    .cp_sizeof = sizeof(struct Component_Body),
    .name = "body",
};

static de_cp_type comp_textured = {
    .cp_id = 2,
    .cp_sizeof = sizeof(struct Component_Textured),
    .name = "textured",
};

static inline void *entt2ptr(de_entity e) {
    return (void*)(uint64_t)e;
}

static inline de_entity ptr2entt(void *p) {
    return (uint32_t)(ptrdiff_t)p;
}

static void create_poly(
    de_entity e,
    cpSpace *space, de_ecs *r, 
    cpVect *verts, int vertsnum, 
    cpTransform transform,
    cpVect *centroid
) {
    assert(space);
    assert(verts);
    assert(r);
    assert(de_valid(r, e));
    struct Component_Body *b = de_emplace(r, e, comp_body);
    //cpVect center = { 300, 300};
    cpVect offset = cpvzero;
    const float radius = 0.;

    cpFloat mass = cpAreaForPoly(vertsnum, verts, 0.0f) * DENSITY;
    trace("create_poly: mass %f\n", mass);
    cpVect _centroid = centroid ? cpvneg(*centroid) : cpvzero;
    cpFloat moment = cpMomentForPoly(mass, vertsnum, verts, _centroid, 0.0f);

    b->b = cpBodyNew(mass, moment);
    b->b->userData = entt2ptr(e);
    cpShape *shape = cpPolyShapeNew(b->b, vertsnum, verts, transform, 0.);
    b->shape = shape;
    cpSpaceAddBody(space, b->b);
    cpSpaceAddShape(space, shape);
}

static void create_box(
    de_entity e, cpSpace *space, de_ecs *r, cpVect center, cpVect wh
) {
    assert(space);
    assert(r);
    float w = wh.x, h = wh.y;
    cpVect verts[4] = {
        cpvsub(center, (cpVect) { -w / 2., h / 2.}),
        cpvsub(center, (cpVect) { -w / 2., -h / 2.}),
        cpvsub(center, (cpVect) { w / 2., -h / 2.}),
        cpvsub(center, (cpVect) { w / 2., h / 2.}),
    };
    const int vertsnum = sizeof(verts) / sizeof(verts[0]);
    cpVect centroid = cpCentroidForPoly(vertsnum, verts);
    create_poly(e, space, r, verts, vertsnum, cpTransformIdentity, &centroid);
}


static void ClipPoly(cpSpace *space, cpShape *shape, cpVect n, cpFloat dist) {
    cpBody *body = cpShapeGetBody(shape);
    
    int count = cpPolyShapeGetCount(shape);
    int clippedCount = 0;
    
    cpVect clipped[count + 1];
    
    for(int i=0, j=count-1; i<count; j=i, i++){
        cpVect a = cpBodyLocalToWorld(body, cpPolyShapeGetVert(shape, j));
        cpFloat a_dist = cpvdot(a, n) - dist;
        
        if(a_dist < 0.0){
            clipped[clippedCount] = a;
            clippedCount++;
        }
        
        cpVect b = cpBodyLocalToWorld(body, cpPolyShapeGetVert(shape, i));
        cpFloat b_dist = cpvdot(b, n) - dist;
        
        if(a_dist*b_dist < 0.0f){
            cpFloat t = cpfabs(a_dist)/(cpfabs(a_dist) + cpfabs(b_dist));
            
            clipped[clippedCount] = cpvlerp(a, b, t);
            clippedCount++;
        }
    }
    

    cpVect centroid = cpCentroidForPoly(clippedCount, clipped);
    de_ecs *r = ((Stage_Splitter*)space->userData)->r;
    cpTransform transform = cpTransformTranslate(cpvneg(centroid));
    de_entity e = de_create(r);
    create_poly(e, space, r, clipped, clippedCount, transform, &centroid);
    struct Component_Body* b = de_get(r, e, comp_body);

    cpBodySetPosition(b->b, centroid);
    cpBodySetVelocity(b->b, cpBodyGetVelocityAtWorldPoint(body, centroid));
    cpBodySetAngularVelocity(b->b, cpBodyGetAngularVelocity(body));
   
    // Copy whatever properties you have set on the original shape that are important
    cpShapeSetFriction(b->shape, cpShapeGetFriction(shape));
}

static void
SliceShapePostStep(cpSpace *space, cpShape *shape, struct SliceContext *context)
{
	cpVect a = context->a;
	cpVect b = context->b;
	
	// Clipping plane normal and distance.
	cpVect n = cpvnormalize(cpvperp(cpvsub(b, a)));
	cpFloat dist = cpvdot(a, n);
	
	ClipPoly(space, shape, n, dist);
	ClipPoly(space, shape, cpvneg(n), -dist);
	
	cpBody *body = cpShapeGetBody(shape);
    cpSpaceRemoveShape(space, shape);
    cpSpaceRemoveBody(space, body);
    cpShapeFree(shape);

    de_ecs *r = ((Stage_Splitter*)space->userData)->r;
    if (body) {
        de_entity e = ptr2entt(body->userData);
        if (de_valid(r, e)) {
            trace("SliceShapePostStep: de_destroy %lu\n", e);
            cpBodyFree(body);
            de_destroy(r, e);
        }
    }
}

static void
SliceQuery(cpShape *shape, cpVect point, cpVect normal, cpFloat alpha, struct SliceContext *context)
{
	cpVect a = context->a;
	cpVect b = context->b;

    /*
    trace(
        "SliceQuery: from %s to %s\n",
        context->a,
        context->b
    );
    */

	// Check that the slice was complete by checking that the endpoints aren't in the sliced shape.
	if(cpShapePointQuery(shape, a, NULL) > 0.0f && cpShapePointQuery(shape, b, NULL) > 0.0f){
		// Can't modify the space during a query.
		// Must make a post-step callback to do the actual slicing.
		cpSpaceAddPostStepCallback(context->space, (cpPostStepFunc)SliceShapePostStep, shape, context);
	}
}

static void create_floor_and_walls(Stage_Splitter *st) {
    const float radius = 1.;
    cpVect a = { 100, 1000 }, b = { 1920 - 100, 1000 };
    float wall_height = 100.;
    //float mass = 100;
    //float moment = cpMomentForSegment(mass, a, b, radius);
    cpBody *body = cpBodyNewStatic();
    cpShape *segment = cpSegmentShapeNew(body, a, b, radius);
    cpSpaceAddBody(st->space, body);
    cpSpaceAddShape(st->space, segment);

    body = cpBodyNewStatic();
    segment = cpSegmentShapeNew(
        body, a, (cpVect) { a.x, a.y - wall_height}, radius
    );
    cpSpaceAddBody(st->space, body);
    cpSpaceAddShape(st->space, segment);

    body = cpBodyNewStatic();
    segment = cpSegmentShapeNew(
        body, b, (cpVect) { b.x, b.y - wall_height}, radius
    );
    cpSpaceAddBody(st->space, body);
    cpSpaceAddShape(st->space, segment);
}

void push_entt(Stage_Splitter *st, de_entity e) {
    assert(st);
    if (st->polygon_num < MAX_ENTITIES)
        st->polygons[st->polygon_num++] = e;
    else {
        trace("push_entt: entities limit reached\n");
        exit(EXIT_FAILURE);
    }
}

RenderTexture2D bake_string(const char *input, int basesize) {
    Vector2 measure = MeasureTextEx(fnt, input, basesize, 0.);
    const float thick = 4.;
    RenderTexture2D tex = LoadRenderTexture(measure.x, measure.y);
    trace(
        "bake_string: tex.width %d tex.height %d\n",
        tex.texture.width,
        tex.texture.height
    );
    BeginTextureMode(tex);
    DrawTextEx(fnt, input, (Vector2) { 0, 0}, fnt.baseSize, 0., WHITE);
    DrawRectangleLinesEx(
        (Rectangle) { 
            .x = 0, .y = 0, .width = measure.x, .height = measure.y,
        }, 
        thick, BLUE
    );
    EndTextureMode();
    return tex;
}

de_entity create_char(
    cpSpace *space, de_ecs *r, const char *input, Vector2 pos
) {
    assert(input);
    de_entity e = de_null;

    e = de_create(r);

    struct Component_Textured *t = de_emplace(r, e, comp_textured);
    t->tex = bake_string(input, fnt.baseSize);
    cpVect sz = { t->tex.texture.width, t->tex.texture.height };
    create_box(e, space, r, from_Vector2(pos), sz); 
    return e;
}

static void create_cp(Stage_Splitter *st) {
    cpSpace *space = st->space = cpSpaceNew();
    space->userData = st;
    cpSpaceSetIterations(space, 30);
    //cpSpaceSetGravity(space, cpv(0, -500));
    cpSpaceSetSleepTimeThreshold(space, 0.5f);
    cpSpaceSetCollisionSlop(space, 0.5f);
    trace("splitter_init: space dumping %f\n", cpSpaceGetDamping(st->space));
    cpSpaceSetDamping(st->space, 0.9);
}

static void _init(Stage_Splitter *st) {
    st->r = de_ecs_make();
    create_cp(st);
    create_floor_and_walls(st);

    push_entt(st, create_char(st->space, st->r, "A", (Vector2) { 200, 100 }));
    push_entt(st, create_char(st->space, st->r, "H", (Vector2) { 1200, 0 }));
}

static void splitter_init(Stage_Splitter *st) {
    trace("splitter_init:\n");

    fnt = load_font_unicode("assets/fonts/VictorMono-Medium.ttf", 455);
    _init(st);
    /*
    e = de_create(st->r);
    create_box(e, st->space, st->r, (cpVect) { 600., -100. });
    push_entt(st, e);
    */
}

static void iter_shape_free(cpBody *body, cpShape *shape, void *data) {
    cpSpace *space = data;
    cpSpaceRemoveShape(space, shape);
    cpShapeFree(shape);
}

static void free_bodies(Stage_Splitter *st) {
    de_view_single view = de_create_view_single(st->r, comp_body);
    while (de_view_single_valid(&view)) {
        struct Component_Body *b = de_view_single_get(&view);
        //cpBodyEachShape(b->b, iter_shape_free, st->space);
        //cpSpaceRemoveBody(st->space, b->b);
        de_view_single_next(&view);
    }
}

static void _shutdown(Stage_Splitter *st) {
    free_bodies(st);
    if (st->space) {
        space_shutdown(st->space, true, true, true);
        cpSpaceFree(st->space);
        st->space = NULL;
    }
    if (st->r) {
        de_ecs_destroy(st->r);
        st->r = NULL;
    }
}

void splitter_shutdown(Stage_Splitter *st) {
    trace("splitter_shutdown:\n");

    _shutdown(st);

    UnloadFont(fnt);
}

void draw_chars(de_ecs *r) {
    de_view view = de_create_view(
        r, 2, (de_cp_type[2]) { comp_body, comp_textured }
    );
    while (de_view_valid(&view)) {
        struct Component_Body *b = de_view_get(&view, comp_body);
        struct Component_Textured *t = de_view_get(&view, comp_textured);

        Rectangle src = {
            0, 0, 
            t->tex.texture.width,
            -t->tex.texture.height,
        };
        Rectangle dst = {
            b->b->p.x,
            b->b->p.y,
            t->tex.texture.width,
            t->tex.texture.height,
        };
        Vector2 origin = {
            t->tex.texture.width / 2.,
            t->tex.texture.height / 2.,
        };

        DrawTexturePro(
            t->tex.texture,
            src,
            dst,
            origin,
            RAD2DEG * b->b->a,
            WHITE
        );

        de_view_next(&view);
    }
}

void splitter_draw(Stage_Splitter *st) {
    //trace("splitter_draw:\n");
    BeginDrawing();
    ClearBackground(BLACK);
    BeginMode2D(cam);

    draw_chars(st->r);

    if (st->space)
        space_debug_draw(st->space, WHITE);
    const float thick = 4.;
    if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)){
        DrawLineEx(from_Vect(sliceStart), GetMousePosition(), thick, RED);
        trace(
            "splitter_draw: %s %s\n",
            Vector2_tostr(from_Vect(sliceStart)),
            Vector2_tostr(GetMousePosition())
        );
    }

    EndMode2D();
    EndDrawing();
}

void splitter_reset(Stage_Splitter *st) {
    _shutdown(st);
    _init(st);
}

void splitter_update(Stage_Splitter *st) {
    /*trace("splitter_update:\n");*/
    if (IsKeyPressed(KEY_ESCAPE)) {
        CloseWindow();
    }

    if (IsKeyPressed(KEY_R)) {
        splitter_reset(st);
    }

    if (IsKeyPressed(KEY_G)) {
        use_gravity = !use_gravity;
        trace(
            "splitter_update: use_gravity %s\n",
            use_gravity ? "false" : "true"
        );
        if (use_gravity)
            cpSpaceSetGravity(st->space, gravity);
        else
            cpSpaceSetGravity(st->space, cpvzero);
    }

    if (IsKeyPressed(KEY_P))
        is_paused = !is_paused;

    if (st->space && !is_paused)
        cpSpaceStep(st->space, 1. / 60);
    //cpSpaceStep(st->space, GetFrameTime());


    // Annoying state tracking code that you wouldn't need
    // in a real event driven system.
    if(IsMouseButtonDown(MOUSE_BUTTON_LEFT) != lastClickState){
        if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)){
            // MouseDown
            sliceStart = from_Vector2(GetMousePosition());
        } else {
            // MouseUp
            cpVect mouse_pos = from_Vector2(GetMousePosition());
            struct SliceContext context = {sliceStart, mouse_pos, st->space};
            trace(
                "splitter_update: from %s to %s\n",
                cpVect_tostr(context.a),
                cpVect_tostr(context.b)
            );

            cpSpaceSegmentQuery(
                st->space, sliceStart, mouse_pos, 
                0.0, GRAB_FILTER, (cpSpaceSegmentQueryFunc)SliceQuery, 
                &context
            );
        }

        lastClickState = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    }

    //trace("splitter_update: sliceStart %s\n", cpVect_tostr(sliceStart));
}

Stage *stage_splitter_new() {
    Stage_Splitter *stage = calloc(1, sizeof(*stage));
    assert(stage);
    stage->parent.draw = (Stage_callback)splitter_draw;
    stage->parent.update = (Stage_callback)splitter_update;
    stage->parent.shutdown = (Stage_callback)splitter_shutdown;
    stage->parent.init = (Stage_data_callback)splitter_init;
    return (Stage*)stage;
}

int main(int argc, char **argv) {
    InitWindow(1920, 1080, "splitter");
    logger_init();
    stage_init();
    stage_add(stage_splitter_new(), "splitter");
    stage_subinit();
    stage_set_active("splitter", NULL);
    cam.zoom = 1.;
    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        stage_update_active();
    }
    stage_shutdown_all();
    logger_shutdown();
    CloseWindow();
    return EXIT_SUCCESS;
}
