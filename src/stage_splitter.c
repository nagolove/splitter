#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_private.h"
#include "chipmunk/chipmunk_types.h"
#include "chipmunk/cpTransform.h"
#include "chipmunk/cpVect.h"
#include "koh_common.h"
#include "koh_console.h"
#include "koh_destral_ecs.h"
#include "koh_dev_draw.h"
#include "koh_hotkey.h"
#include "koh_logger.h"
#include "koh_object.h"
#include "koh_render.h"
#include "koh_render.h"
#include "koh_routine.h"
#include "koh_stages.h"
#include "raylib.h"
#include "raymath.h"
#include "stage_splitter.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

enum {
    HOTKEY_GROUP_SPLITTER = 0b0000100,
};

static Camera2D cam = {0};
static Camera2D default_cam = {0};
static cpVect gravity = { 0, 9.8 * 20. };
static bool use_gravity = false;
static cpBool lastClickState = cpFalse;
static cpVect sliceStart = {0.0, 0.0};
static Font fnt = {0};
static bool is_paused = false;
static Shader shdr_mask = {0};
static int loc_mask_tex = 0;

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
    //cpShape *shape;
};

struct Component_Textured {
    RenderTexture2D tex, mask;
    cpTransform     tr;
};

static void _init(Stage_Splitter *st);
static void _shutdown(Stage_Splitter *st);

static de_cp_type comp_body = {
    .cp_id = 1,
    .cp_sizeof = sizeof(struct Component_Body),
    .name = "body",
};

void on_destroy_textured(void *payload, de_entity e);

static de_cp_type comp_textured = {
    .cp_id = 2,
    .cp_sizeof = sizeof(struct Component_Textured),
    .name = "textured",
    .on_destroy = on_destroy_textured,
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
    cpTransform transform
    //cpVect *centroid
) {
    assert(space);
    assert(verts);
    assert(r);
    assert(de_valid(r, e));
    struct Component_Body *b = de_emplace(r, e, comp_body);

    cpFloat mass = cpAreaForPoly(vertsnum, verts, 0.0f) * DENSITY;
    trace("create_poly: mass %f\n", mass);
    cpVect centroid = cpCentroidForPoly(vertsnum, verts);
    cpFloat moment = cpMomentForPoly(mass, vertsnum, verts, centroid, 0.0f);

    b->b = cpBodyNew(mass, moment);
    b->b->userData = entt2ptr(e);
    cpShape *shape = cpPolyShapeNew(b->b, vertsnum, verts, transform, 0.);
    //b->shape = shape;
    cpSpaceAddBody(space, b->b);
    cpSpaceAddShape(space, shape);
}

static void create_circle(
    de_entity e, cpSpace *space, de_ecs *r, cpVect center, float radius
) {
    assert(space);
    assert(r);
    /*const int vertsnum = sizeof(verts) / sizeof(verts[0]);*/
    /*create_poly(e, space, r, verts, vertsnum, cpTransformIdentity);*/
    /*struct Component_Body *b = de_get(r, e, comp_body);*/
    /*cpBodySetPosition(b->b, center);*/
}

static void create_box(
    de_entity e, cpSpace *space, de_ecs *r, cpVect center, cpVect wh
) {
    assert(space);
    assert(r);
    float w = wh.x, h = wh.y;
    cpVect verts[4] = {
        { -w / 2., h / 2.},
        { -w / 2., -h / 2.},
        { w / 2., -h / 2.},
        { w / 2., h / 2.},
    };
    const int vertsnum = sizeof(verts) / sizeof(verts[0]);
    create_poly(e, space, r, verts, vertsnum, cpTransformIdentity);
    struct Component_Body *b = de_get(r, e, comp_body);
    cpBodySetPosition(b->b, center);
}

/*
static void iter_shape_contor(cpBody *body, cpShape *shape, void *data) {
    Vector2 tri_strip[250] = {0};
    int num = cpPolyShapeGetCount(shape);

    assert(num < (sizeof(tri_strip) / sizeof(Vector2)));
    if (num >= (sizeof(tri_strip) / sizeof(Vector2)))
        return;

    int j = 0, i = 1;

    while (i < num) {
        tri_strip[j++] = from_Vect(
            cpBodyLocalToWorld(body, cpPolyShapeGetVert(shape, 0)));
        tri_strip[j++] = from_Vect(
            cpBodyLocalToWorld(body, cpPolyShapeGetVert(shape, i)));
        if (i + 1 < num)
            tri_strip[j++] = from_Vect(cpBodyLocalToWorld(
                body ,cpPolyShapeGetVert(shape, i + 1)
            ));
        i++;
    }

    DrawTriangleStrip(tri_strip, j - 1, GREEN);

    trace("iter_shape_contour: ");
    for (int i = 0; i < num; i++) {
        trace("%d ", tri_strip[i]);
    }
    trace("\n");

    cpBB bb = cpShapeGetBB(shape);
    bb_draw(bb_world2local(body, bb), BLUE);
    trace("iter_shape_contour: %s\n", bb_tostr(bb));
}
*/

static void iter_shape_contour(cpBody *body, cpShape *shape, void *data) {
    Vector2 tri_strip[250] = {0};
    int num = cpPolyShapeGetCount(shape);

    assert(num < (sizeof(tri_strip) / sizeof(Vector2)));
    if (num >= (sizeof(tri_strip) / sizeof(Vector2)))
        return;

    int j = 0, i = 1;

    while (i < num) {
        tri_strip[j++] = from_Vect(cpPolyShapeGetVert(shape, 0));
        tri_strip[j++] = from_Vect(cpPolyShapeGetVert(shape, i));
        if (i + 1 < num)
            tri_strip[j++] = from_Vect(cpPolyShapeGetVert(shape, i + 1));
        i++;
    }

    Camera2D _cam = {0};
    Rectangle bb_rect = from_bb(cpShapeGetBB(shape));
    _cam.zoom = .5;
    _cam.offset = (Vector2) { bb_rect.width / 2., bb_rect.height / 2. };
    BeginMode2D(_cam);

    DrawTriangleStrip(tri_strip, j - 1, random_raylib_color());

    trace("iter_shape_contour: %p\n", body);
    trace("iter_shape_contour: shapeList %p\n", body->shapeList);
    for (i = 0; i < num; i++) {
        trace("%d ", tri_strip[i]);
    }
    trace("\n");

    cpBB bb = cpShapeGetBB(shape);
    bb_draw(bb_world2local(body, bb), BLUE);
    EndMode2D();
    trace("iter_shape_contour: bb %s\n", bb_tostr(bb));
    trace("iter_shape_contour: rect %s\n", rect2str(from_bb(bb)));
}

/*
Как рисовать контур в целевую текстуру - рисовать контуры всех форм или только
одной? Контуры могут быть далеко разнесены в пределах тела. Использовать-ли
масштабирование к размеру целевой текстуры?
*/
static void render_contour(RenderTexture2D *target, cpBody *b) {
    assert(b);
    assert(target);

    trace(
        "render_contour: %dx%d\n",
        target->texture.width,
        target->texture.height
    );

    BeginTextureMode(*target);

    //BeginMode2D(_cam);
    ClearBackground(WHITE);

    cpBodyEachShape(b, iter_shape_contour, NULL);
    //EndMode2D();
    EndTextureMode();

#if 1
    Image img = LoadImageFromTexture(target->texture);
    char fname[128] = {0};
    sprintf(fname, "toasts/%p.png", b);
    ExportImage(img, fname);
    UnloadImage(img);
#endif
}

struct ShapeCopyCtx {
    float friction;
};

static void iter_shape_copy(cpBody *body, cpShape *shape, void *data) {
    struct ShapeCopyCtx *ctx = data;
    cpShapeSetFriction(shape, ctx->friction);
}

static de_entity ClipPoly(cpSpace *space, cpShape *shape, cpVect n, cpFloat dist) {
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
    create_poly(e, space, r, clipped, clippedCount, transform);
    struct Component_Body* b = de_get(r, e, comp_body);
    assert(b);

    cpBodySetPosition(b->b, centroid);
    cpBodySetVelocity(b->b, cpBodyGetVelocityAtWorldPoint(body, centroid));
    cpBodySetAngularVelocity(b->b, cpBodyGetAngularVelocity(body));
   
    // Copy whatever properties you have set on the original shape that are important
    struct ShapeCopyCtx ctx_copy = {
        .friction = cpShapeGetFriction(shape),
    };
    cpBodyEachShape(b->b, iter_shape_copy, &ctx_copy);
    return e;
}

static RenderTexture2D clone_render_texture(RenderTexture2D src) {
    RenderTexture2D dst = LoadRenderTexture(
        src.texture.width, src.texture.height
    );
    assert(dst.id != 0);
    Camera2D cam_local = {0};
    cam_local.zoom = 1.;

    BeginMode2D(cam_local);
    BeginTextureMode(dst);
    DrawTexture(src.texture, 0, 0, WHITE);
    EndMode2D();
    return dst;
}

static void update_mask(
    de_ecs *r, de_entity e_new, de_entity e_old, cpBody *body
) {
    struct Component_Textured *t = de_try_get(r, e_old, comp_textured);
    if (!t) {
        trace("SliceShapePostStep: t == NULL\n");
        return;
    }

    struct Component_Textured *t_new = de_emplace(r, e_new, comp_textured);
    t_new->tex = clone_render_texture(t->tex);
    t_new->mask = clone_render_texture(t->mask);
    BeginTextureMode(t_new->mask);

    /*default_cam.offset = from_Vect(body->p);*/

    Camera2D _cam = {0};
    _cam.zoom = 0.8;
    BeginMode2D(_cam);
    render_contour(&t_new->mask, body);
    /*DrawTexture(t->mask.texture, 0, 0, YELLOW);*/
    /*DrawCircle(1, 1, 1000, YELLOW);*/
    EndMode2D();
    EndTextureMode();
}

static void
SliceShapePostStep(cpSpace *space, cpShape *shape, struct SliceContext *context)
{
    de_ecs *r = ((Stage_Splitter*)space->userData)->r;
    cpVect a = context->a;
    cpVect b = context->b;
    
    // Clipping plane normal and distance.
    cpVect n = cpvnormalize(cpvperp(cpvsub(b, a)));
    cpFloat dist = cpvdot(a, n);
    
    de_entity e_new1 = de_null, e_new2 = de_null;

    cpBody *body = cpShapeGetBody(shape);
    e_new1 = ClipPoly(space, shape, n, dist);

    de_entity e_old = ptr2entt(body->userData);
    update_mask(r, e_new1, e_old, body);

    e_new2 = ClipPoly(space, shape, cpvneg(n), -dist);
    update_mask(r, e_new2, e_old, body);
    
    cpSpaceRemoveShape(space, shape);
    cpSpaceRemoveBody(space, body);
    cpShapeFree(shape);

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

    if (shape->klass->type != CP_POLY_SHAPE) {
        return;
    }

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
    cpBodyAddShape(body, segment);
    cpSpaceAddShape(st->space, segment);

    body = cpBodyNewStatic();
    segment = cpSegmentShapeNew(
        body, a, (cpVect) { a.x, a.y - wall_height}, radius
    );
    cpSpaceAddBody(st->space, body);
    cpBodyAddShape(body, segment);
    cpSpaceAddShape(st->space, segment);

    body = cpBodyNewStatic();
    segment = cpSegmentShapeNew(
        body, b, (cpVect) { b.x, b.y - wall_height}, radius
    );
    cpSpaceAddBody(st->space, body);
    cpBodyAddShape(body, segment);
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
    cpSpace *space, de_ecs *r, const char *input, Vector2 abs_pos
) {
    assert(input);
    de_entity e = de_null;

    e = de_create(r);

    struct Component_Textured *t = de_emplace(r, e, comp_textured);
    t->tr = cpTransformIdentity;
    t->tex = bake_string(input, fnt.baseSize);
    t->mask = LoadRenderTexture(t->tex.texture.width, t->tex.texture.height);
    cpVect sz = { t->tex.texture.width, t->tex.texture.height };

    create_box(e, space, r, from_Vector2(abs_pos), sz); 
    struct Component_Body *b = de_try_get(r, e, comp_body);
    assert(b);

    render_contour(&t->mask, b->b);
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
    memset(&cam, 0, sizeof(cam));
    cam.zoom = 1.;

    st->r = de_ecs_make();
    create_cp(st);
    create_floor_and_walls(st);

    push_entt(st, create_char(st->space, st->r, "A", (Vector2) { 200, 100 }));
    push_entt(st, create_char(st->space, st->r, "H", (Vector2) { 1200, 0 }));
    push_entt(st, create_char(st->space, st->r, "J", (Vector2) { 200, 600, }));
}

static void hk_remove_body(Hotkey *hk) {
    trace("hk_remove_body:\n");
}

static void splitter_init(Stage_Splitter *st) {
    trace("splitter_init:\n");

    fnt = load_font_unicode("assets/fonts/VictorMono-Medium.ttf", 455);
    shdr_mask = LoadShader(NULL, "assets/vertex/100_fragment_stencil.glsl");
    loc_mask_tex = GetShaderLocation(shdr_mask, "mask_texture");

    _init(st);
    cam.zoom = 1.;
    default_cam.zoom = 1.;

    assert(st->parent.data);
    struct SplitterCtx *ctx = st->parent.data;
    hotkey_register(ctx->hk_store, (Hotkey) {
        .name = "remove",
        .description = "Удалить объект под курсором мыши.",
        .func = hk_remove_body,
        .data = NULL,
        .groups = HOTKEY_GROUP_SPLITTER,
        .combo = {
            .mode = HM_MODE_ISKEYDOWN,
            .key = KEY_X,
        },
    });
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
    UnloadShader(shdr_mask);
}

void draw_chars(de_ecs *r, de_entity *ennts, int entts_num) {
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

        SetShaderValueTexture(shdr_mask, loc_mask_tex, t->mask.texture);
        BeginShaderMode(shdr_mask);
        render_texture_t(
            t->tex.texture, src, dst, origin, RAD2DEG * b->b->a,
            WHITE, t->tr
        );
        EndShaderMode();
        DrawCircle(b->b->p.x, b->b->p.y, 10, BLUE);

        de_view_next(&view);
    }

}

static void debug_draw_textures_and_masks(de_ecs *r, Vector2 start_point) {
    const float thick = 4.;

    Vector2 point = start_point;
    de_view_single v = de_create_view_single(r, comp_textured);
    while (de_view_single_valid(&v)) {
        struct Component_Textured *t = de_view_single_get(&v);
        DrawTexture(t->tex.texture, point.x, point.y, WHITE);
        DrawRectangleLinesEx(
            (Rectangle) {
                point.x, point.y, t->tex.texture.width, t->tex.texture.height,
            },
            thick, BLUE
        );
        DrawTexture(
            t->mask.texture,
            point.x,
            point.y + t->tex.texture.height,
            WHITE
        );
        DrawRectangleLinesEx(
            (Rectangle) {
                point.x, point.y + t->tex.texture.height,
                t->tex.texture.width, t->tex.texture.height,
            },
            thick, BLUE
        );

        point.x += t->tex.texture.width + thick;

        de_view_single_next(&v);
    }
}

void splitter_draw(Stage_Splitter *st) {
    //trace("splitter_draw:\n");
    BeginDrawing();
    ClearBackground(BLACK);
    BeginMode2D(cam);

    draw_chars(st->r, st->polygons, st->polygon_num);
    debug_draw_textures_and_masks(st->r, (Vector2) { -2000, -1100, });

    if (st->space)
        space_debug_draw(st->space, WHITE);

    EndMode2D();

    const float thick = 4.;
    if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)){
        DrawLineEx(
            from_Vect(sliceStart), 
            GetScreenToWorld2D(GetMousePosition(), cam),
            thick, RED
        );
        /*DrawLineEx(from_Vect(sliceStart), GetMousePosition(), thick, RED);*/
        trace(
            "splitter_draw: %s %s\n",
            Vector2_tostr(from_Vect(sliceStart)),
            Vector2_tostr(GetMousePosition())
        );
    }
    EndDrawing();
    draw_camera_axis(&cam, (struct CameraAxisDrawCtx) {
        .color_offset = BLUE,
        .color_target = RED,
        .fnt_size = 25,
    });

    //console_buf_write_c(WHITE, "sliceStart %s", cpVect_tostr(sliceStart));
    console_write("sliceStart %s", cpVect_tostr(sliceStart));
    console_write("cam %s", camera2str(cam));
}

void splitter_reset(Stage_Splitter *st) {
    _shutdown(st);
    _init(st);
}

static void camera_process_mouse_wheel(Camera2D *cam) {
    float wheel = GetMouseWheelMove();
    if (wheel == 0.) 
        return;
    Vector2 mouse_world_pos = GetScreenToWorld2D(GetMousePosition(), *cam);
    cam->offset = GetMousePosition();
    cam->target = mouse_world_pos;
    const float dzoom = 0.098;
    cam->zoom += wheel * dzoom;
    cam->zoom = cam->zoom < dzoom ? dzoom : cam->zoom;
}

static void xxx_draw_slice(void *udata) {
    struct SliceContext *ctx = udata;
    DrawLineV(from_Vect(ctx->a), from_Vect(ctx->b), BLUE);
}

void splitter_update(Stage_Splitter *st) {
    /*trace("splitter_update:\n");*/
    if (IsKeyPressed(KEY_ESCAPE)) {
        CloseWindow();
    }

    if (IsKeyPressed(KEY_C)) {
        trace("splitter_update: cam reset\n");
        cam.offset = Vector2Zero();
        cam.target = Vector2Zero();
        cam.zoom = 1.;
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

    if (st->space && !is_paused) cpSpaceStep(st->space, 1. / 60);
    //cpSpaceStep(st->space, GetFrameTime());
    
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        int ch = random() % 26;
        char input[2] = {0};
        input[0] = 'A' + ch;
        create_char(
            st->space, st->r, input, 
            GetScreenToWorld2D(GetMousePosition(), cam)
        );
    }

    camera_process_mouse_wheel(&cam);
    camera_process_mouse_drag(MOUSE_BUTTON_MIDDLE, &cam);

    // Annoying state tracking code that you wouldn't need
    // in a real event driven system.
    if(IsMouseButtonDown(MOUSE_BUTTON_LEFT) != lastClickState){
        if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)){
            Vector2 world_pos = GetScreenToWorld2D(GetMousePosition(), cam);
            sliceStart = from_Vector2(world_pos);
        } else {
            Vector2 world_pos = GetScreenToWorld2D(GetMousePosition(), cam);
            cpVect mouse_pos = from_Vector2(world_pos);
            struct SliceContext context = {sliceStart, mouse_pos, st->space};
            trace(
                "splitter_update: from %s to %s\n",
                cpVect_tostr(context.a),
                cpVect_tostr(context.b)
            );

            dev_draw_push(xxx_draw_slice, &context, sizeof(context));

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

void on_destroy_textured(void *payload, de_entity e) {
    assert(payload);
    struct Component_Textured *t = payload;
    UnloadRenderTexture(t->tex);
    UnloadRenderTexture(t->mask);
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

void stage_splitter_test() {
#if 1
    cpSpace *space = cpSpaceNew();

    de_ecs *ecs = de_ecs_make();

    {
        de_entity e = create_char(space, ecs, "HUI", (Vector2) { 100, 100});
        struct Component_Textured *t = de_try_get(ecs, e, comp_textured);
        struct Component_Body *b = de_try_get(ecs, e, comp_body);
        assert(t);
        assert(b);
        render_contour(&t->mask, b->b);
    }

    {
        de_entity e = create_char(space, ecs, "HUI", (Vector2) { 100, 100});
        struct Component_Textured *t = de_try_get(ecs, e, comp_textured);
        struct Component_Body *b = de_try_get(ecs, e, comp_body);
        cpBodyAddShape(b->b, make_circle_polyshape(b->b, 40., NULL));
        assert(t);
        assert(b);
        render_contour(&t->mask, b->b);
    }


    de_ecs_destroy(ecs);

    space_shutdown(space, true, true, true);
    cpSpaceFree(space);
#endif
}

