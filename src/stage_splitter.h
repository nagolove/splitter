#pragma once

#include "koh_hotkey.h"
#include "koh_stages.h"
#include "koh_hotkey.h"

struct SplitterCtx {
    HotkeyStorage *hk_store;
};

Stage *stage_splitter_new();
void stage_splitter_test();
