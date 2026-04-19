#include "network_internal.h"

#if defined(__x86_64__)
#include "net/http.h"
#include "security/tls.h"
#endif

#if defined(__x86_64__)
static int net_query_lookup_target_ip(const char *language, const char *target,
                                      const struct net_stack_status *st,
                                      uint32_t *out_ip) {
  uint32_t alias_ip = 0;

  if (!target || !st || !out_ip) {
    return -1;
  }

  alias_ip = net_cli_resolve_target_ip(target, st);
  if (alias_ip != 0u) {
    *out_ip = alias_ip;
    return 0;
  }
  if (net_cli_parse_ipv4(target, out_ip) == 0) {
    return 0;
  }
  if (net_stack_dns_resolve(target, 2500u, out_ip) == 0) {
    return 1;
  }
  (void)language;
  return -2;
}
#endif

#if defined(__x86_64__)
static void net_query_print_tls_summary(void) {
  struct tls_security_info info;
  if (tls_get_last_security_info(&info) != 0 || info.protocol_version == 0) {
    shell_print("tls=none\n");
    return;
  }
  shell_print("tls=");
  shell_print(tls_version_name(info.protocol_version));
  shell_print(" cipher=");
  shell_print(tls_cipher_suite_name(info.cipher_suite));
  shell_print(" host=");
  shell_print(info.hostname_validated ? "ok" : "skip");
  shell_print(" peer=");
  shell_print(info.peer_verified ? "ok" : "fail");
  shell_print(" alpn=");
  shell_print(info.alpn[0] ? info.alpn : "(none)");
  shell_newline();
}

static int net_query_body_looks_textual(const uint8_t *body, size_t len) {
  size_t sample_len = len;
  size_t printable = 0;
  if (!body || len == 0) return 0;
  if (sample_len > 512) sample_len = 512;
  for (size_t i = 0; i < sample_len; i++) {
    uint8_t ch = body[i];
    if (ch == 0) return 0;
    if (ch == '\t' || ch == '\n' || ch == '\r' || (ch >= 32 && ch < 127)) {
      printable++;
    }
  }
  return printable * 100 >= sample_len * 80;
}

static void net_query_print_body_preview(const uint8_t *body, size_t len) {
  char preview[193];
  size_t out = 0;
  if (!body || len == 0 || !net_query_body_looks_textual(body, len)) return;
  for (size_t i = 0; i < len && out + 1 < sizeof(preview) && out < 192; i++) {
    uint8_t ch = body[i];
    if (ch == '\r') continue;
    if (ch == '\n' || ch == '\t') {
      preview[out++] = ' ';
    } else if (ch >= 32 && ch < 127) {
      preview[out++] = (char)ch;
    } else {
      preview[out++] = '.';
    }
  }
  while (out > 0 && preview[out - 1] == ' ') out--;
  preview[out] = '\0';
  if (!preview[0]) return;
  shell_print("preview=");
  shell_print(preview);
  shell_newline();
}
#endif

int net_cmd_resolve(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-resolve <hostname>",
                        net_cli_text(language, NET_HELP_RESOLVE))) {
    return 0;
  }

  if (argc != 2) {
    shell_print_error(net_cli_text(language, NET_INVALID_USAGE));
    shell_suggest_help("net-resolve");
    return -1;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  if (!net_stack_ready()) {
    struct net_stack_status st;
    shell_print_error(net_cli_text(language, NET_STACK_NOT_READY));
    if (net_cli_read_status(language, &st) == 0) {
      net_cli_print_runtime_block_detail(&st);
    }
    return -1;
  }

  uint32_t resolved_ip = 0;
  if (net_stack_dns_resolve(argv[1], 2500u, &resolved_ip) != 0) {
    shell_print_error(net_cli_text(language, NET_DNS_LOOKUP_FAILED));
    return -1;
  }

  char ip[16];
  net_ipv4_format(resolved_ip, ip);
  shell_print("name=");
  shell_print(argv[1]);
  shell_print(" ipv4=");
  shell_print(ip);
  shell_newline();
  return 0;
#endif
}

int net_cmd_fetch(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "net-fetch <url>",
                        net_cli_text(language, NET_HELP_FETCH))) {
    return 0;
  }
  if (argc != 2) {
    shell_print_error(net_cli_text(language, NET_INVALID_USAGE));
    shell_suggest_help("net-fetch");
    return -1;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  struct net_stack_status st;
  struct http_response resp;
  char host[HTTP_MAX_HOST];
  char path[HTTP_MAX_PATH];
  uint16_t port = 0;
  int use_tls = 0;
  const char *content_type = NULL;
  const char *location = NULL;

  if (!net_stack_ready()) {
    shell_print_error(net_cli_text(language, NET_STACK_NOT_READY));
    if (net_cli_read_status(language, &st) == 0) {
      net_cli_print_runtime_block_detail(&st);
    }
    return -1;
  }

  if (http_parse_url(argv[1], host, sizeof(host), path, sizeof(path), &port, &use_tls) != 0) {
    shell_print_error("invalid url");
    return -1;
  }

  if (http_get(argv[1], &resp) != 0) {
    shell_print_error(http_error_string(http_last_error()));
    if (use_tls) net_query_print_tls_summary();
    return -1;
  }

  content_type = http_find_header(&resp, "Content-Type");
  location = http_find_header(&resp, "Location");
  shell_print("status=");
  shell_print_number((uint32_t)resp.status_code);
  shell_print(" host=");
  shell_print(host);
  shell_print(" port=");
  shell_print_number((uint32_t)port);
  shell_print(" body=");
  shell_print_number((uint32_t)resp.body_len);
  shell_newline();

  shell_print("content-type=");
  shell_print(content_type && content_type[0] ? content_type : "(none)");
  shell_newline();

  if (location && location[0]) {
    shell_print("location=");
    shell_print(location);
    shell_newline();
  }

  if (use_tls) net_query_print_tls_summary();
  net_query_print_body_preview(resp.body, resp.body_len);
  http_response_free(&resp);
  return 0;
#endif
}

int net_cmd_ping(struct shell_context *ctx, int argc, char **argv) {
  const char *language = shell_current_language();
  (void)ctx;
  if (shell_handle_help(argc, argv, "hey <ip|hostname|gateway|dns|self>",
                        net_cli_text(language, NET_HELP_HEY))) {
    return 0;
  }
  if (argc < 2) {
    shell_print_error(net_cli_text(language, NET_REQUIRE_DESTINATION));
    shell_suggest_help("hey");
    return -1;
  }

#if !defined(__x86_64__)
  shell_print_error(net_cli_text(language, NET_COMMAND_UNSUPPORTED));
  return -1;
#else
  if (!net_stack_ready()) {
    struct net_stack_status st;
    shell_print_error(net_cli_text(language, NET_STACK_NOT_READY));
    if (net_cli_read_status(language, &st) == 0) {
      net_cli_print_runtime_block_detail(&st);
    }
    return -1;
  }

  const char *target = argv[1];
  struct net_stack_status st;
  if (net_cli_read_status(language, &st) != 0) {
    return -1;
  }
  uint32_t dst_ip = 0;
  int resolved_via_dns =
      net_query_lookup_target_ip(language, target, &st, &dst_ip);
  if (resolved_via_dns < 0) {
    shell_print_error(net_cli_text(language, NET_DNS_LOOKUP_FAILED));
    return -1;
  }

  uint32_t rtt_ms = 0;
  uint32_t reply_ip = 0;
  if (net_stack_ping(dst_ip, 1200u, &rtt_ms, &reply_ip) != 0) {
    shell_print_error(net_cli_text(language, NET_NO_REPLY));
    return -1;
  }
  if (rtt_ms == 0) {
    rtt_ms = 1;
  }

  char dst_text[16];
  char reply_text[16];
  net_ipv4_format(dst_ip, dst_text);
  net_ipv4_format(reply_ip ? reply_ip : dst_ip, reply_text);

  shell_print("hello from (");
  if (resolved_via_dns > 0) {
    shell_print(target);
    shell_print("/");
  }
  shell_print(dst_text);
  shell_print("/");
  shell_print(reply_text);
  shell_print(") ");
  shell_print_number(rtt_ms);
  shell_print("ms)\n");
  return 0;
#endif
}
