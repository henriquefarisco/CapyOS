int run_html_viewer_tests(void) {
  int fails = 0;
  fails += test_html_entities_and_links();
  fails += test_html_skips_script_and_keeps_noscript();
  fails += test_html_parses_basic_forms();
  fails += test_html_handles_boolean_attributes();
  fails += test_html_parses_lazy_image_sources();
  fails += test_html_viewer_open_quick_start();
  fails += test_html_viewer_http_navigation_finishes_loading();
  fails += test_html_viewer_redirects_update_final_url();
  fails += test_html_viewer_all_common_redirect_statuses();
  fails += test_html_viewer_redirect_limit_fails_controlled();
  fails += test_html_viewer_escape_cancels_loading_navigation();
  fails += test_html_viewer_loads_external_assets();
  fails += test_html_viewer_http_cache_enforces_memory_budget();
  fails += test_html_viewer_about_metrics_pages();
  fails += test_html_viewer_discards_stale_response();
  fails += test_html_viewer_safe_mode_on_large_document();
  fails += test_html_viewer_rejects_script_document();
  fails += test_html_viewer_extracts_html_after_bootstrap_noise();
  fails += test_html_viewer_degrades_script_only_html_shell();
  fails += test_html_viewer_follows_meta_refresh_redirect();
  fails += test_html_viewer_uses_base_href_for_assets();
  fails += test_html_viewer_marks_unsupported_image_format();
  fails += test_html_viewer_loads_stylesheet_rel_tokens_and_imports();
  fails += test_html_viewer_loads_style_block_imports_once();
  if (fails == 0) printf("[PASS] html_viewer\n");
  return fails;
}

