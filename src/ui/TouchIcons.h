#pragma once
// Semantic icon names for the im-touch shell (and, later, the shared tool
// catalogue). One indirection over the raw Iconoir glyphs so a design pass
// can swap a glyph in exactly one place — UI code says MZ_ICON_UNDO, never
// ICON_IC_UNDO. The full Iconoir range is merged into the font atlas at
// startup (Application ctor), so any ICON_IC_* glyph is always renderable.
//
// Iconoir: https://iconoir.com (MIT) — see assets/fonts/FONT-CREDITS.md.
#include "IconsIconoir.h"

// Chrome / top bar
#define MZ_ICON_UNDO       ICON_IC_UNDO
#define MZ_ICON_REDO       ICON_IC_REDO
#define MZ_ICON_MORE       ICON_IC_MORE_HORIZ
#define MZ_ICON_FOCUS      ICON_IC_FRAME_SELECT
#define MZ_ICON_FULL       ICON_IC_EXPAND
#define MZ_ICON_FULL_EXIT  ICON_IC_COLLAPSE
#define MZ_ICON_CLOSE      ICON_IC_XMARK
#define MZ_ICON_SETTINGS   ICON_IC_SETTINGS
#define MZ_ICON_ABOUT      ICON_IC_INFO_CIRCLE

// Files
#define MZ_ICON_OPEN       ICON_IC_FOLDER
#define MZ_ICON_SAVE       ICON_IC_FLOPPY_DISK
#define MZ_ICON_SAVE_AS    ICON_IC_FLOPPY_DISK_ARROW_OUT

// Right panel
#define MZ_ICON_ITEMS      ICON_IC_LIST
#define MZ_ICON_HISTORY    ICON_IC_CLOCK_ROTATE_RIGHT
#define MZ_ICON_VISIBLE    ICON_IC_EYE
#define MZ_ICON_HIDDEN     ICON_IC_EYE_CLOSED
#define MZ_ICON_EDIT       ICON_IC_EDIT_PENCIL
#define MZ_ICON_DELETE     ICON_IC_TRASH
#define MZ_ICON_ADD        ICON_IC_PLUS
#define MZ_ICON_CHECK      ICON_IC_CHECK

// Tools (Phase 3 rail)
#define MZ_ICON_SKETCH     ICON_IC_DESIGN_PENCIL
#define MZ_ICON_EXTRUDE    ICON_IC_EXTRUDE
#define MZ_ICON_PUSHPULL   ICON_IC_ENLARGE
#define MZ_ICON_SHELL      ICON_IC_CUBE_HOLE
#define MZ_ICON_FILLET     ICON_IC_FILLET_3D
#define MZ_ICON_CHAMFER    ICON_IC_CUBE_CUT_WITH_CURVE
#define MZ_ICON_MOVE       ICON_IC_DRAG
#define MZ_ICON_ROTATE     ICON_IC_REFRESH
#define MZ_ICON_SCALE      ICON_IC_SCALE_FRAME_ENLARGE
#define MZ_ICON_MIRROR     ICON_IC_MIRROR
#define MZ_ICON_MEASURE    ICON_IC_RULER
#define MZ_ICON_AXES       ICON_IC_AXES
