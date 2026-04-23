/*
 * CapyBrowser is currently split into implementation fragments while it keeps
 * a single translation unit. This preserves the existing private static state
 * and gives us readable module boundaries before the next .c/.o extraction.
 */

#define HTML_VIEWER_INTERNAL_ORCHESTRATOR 1
#include "html_viewer/internal/html_viewer_internal.h"
#include "html_viewer/html_tree_helpers.inc"
#include "html_viewer/ui_shell.inc"
#include "html_viewer/render_primitives.inc"
#include "html_viewer/render_tree.inc"
#include "html_viewer/html_parser.inc"
#include "html_viewer/app_entry_async.inc"
#include "html_viewer/resource_loading.inc"
#include "html_viewer/public_api.inc"
