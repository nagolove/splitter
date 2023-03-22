#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_types.h"
#include "chipmunk/cpVect.h"
#include "koh_common.h"
#include "koh_console.h"
#include "koh_destral_ecs.h"
#include "koh_dev_draw.h"
#include "koh_hotkey.h"
#include "koh_logger.h"
#include "koh_object.h"
#include "koh_render.h"
#include "koh_routine.h"
#include "koh_script.h"
#include "koh_stages.h"
#include "raylib.h"
#include "raymath.h"
#include "stage_splitter.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    SetConfigFlags(FLAG_FULLSCREEN_MODE | FLAG_MSAA_4X_HINT);
    SetExitKey(KEY_NULL);
    InitWindow(1920 * 2, 1080 * 2, "splitter");
    logger_init();
    sc_init();

    HotkeyStorage hk_store = {0};
    hotkey_init(&hk_store);

    console_init(&hk_store, &(struct ConsoleSetup) { 
        .fnt_path = "assets/fonts/VictorMono-Medium.ttf",
        .fnt_size = 40,
        .on_disable = NULL,
        .on_enable = NULL,
        .udata = NULL,
        .color_text = GRAY,
        .color_cursor = GOLD,
    });
    console_immediate_buffer_enable(true);

    dev_draw_init();
    dev_draw_enable(true);

    stage_init();

    static struct SplitterCtx ctx = {0};
    ctx.hk_store = &hk_store;

    Stage *st = stage_add(stage_splitter_new(), "splitter");
    st->data = &ctx;

    stage_subinit();
    stage_set_active("splitter", NULL);
    SetTargetFPS(60);

    stage_splitter_test();

    while (!WindowShouldClose()) {
        hotkey_process(&hk_store);

        stage_update_active();

        console_check_editor_mode();
        console_update();
        dev_draw_draw();
    }
    stage_shutdown_all();

    dev_draw_shutdown();
    hotkey_shutdown(&hk_store);
    console_shutdown();
    logger_shutdown();
    CloseWindow();
    return EXIT_SUCCESS;
}

