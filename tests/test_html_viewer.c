/*
 * Browser tests are grouped as fragments to keep the test runner entrypoint
 * stable while parser, stub, navigation, resource and compatibility cases
 * become readable in isolation.
 */

#include "html_viewer/parser_cases.inc"
#include "html_viewer/test_runner.inc"
#include "html_viewer/gui_font_stubs.inc"
#include "html_viewer/http_stubs.inc"
#include "html_viewer/network_tls_stubs.inc"
#include "html_viewer/navigation_cases.inc"
#include "html_viewer/resource_cases.inc"
#include "html_viewer/compatibility_cases.inc"
