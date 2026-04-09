#include <stdio.h>
#include <string.h>
#include "memory/pmm.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-40s ", name);
#define PASS() printf("OK\n"); tests_passed++; } while(0)
#define FAIL(msg) printf("FAIL: %s\n", msg); } while(0)

static struct pmm_region test_regions[2];

void test_pmm_init(void) {
  TEST("pmm_init with valid region");
  test_regions[0].base = 0x100000;   /* 1 MiB */
  test_regions[0].length = 0x400000; /* 4 MiB */
  test_regions[0].type = 1;          /* usable */
  pmm_init(test_regions, 1);
  struct pmm_stats st;
  pmm_stats_get(&st);
  if (st.total_pages > 0 && st.free_pages > 0) { PASS(); }
  else { FAIL("no pages after init"); }
}

void test_pmm_alloc_free(void) {
  TEST("pmm_alloc_page returns non-zero");
  uint64_t page = pmm_alloc_page();
  if (page != 0) { PASS(); }
  else { FAIL("alloc returned 0"); }

  TEST("pmm_free_page restores count");
  struct pmm_stats before, after;
  pmm_stats_get(&before);
  pmm_free_page(page);
  pmm_stats_get(&after);
  if (after.free_pages == before.free_pages + 1) { PASS(); }
  else { FAIL("free count mismatch"); }
}

void test_pmm_alloc_pages(void) {
  TEST("pmm_alloc_pages contiguous");
  uint64_t pages = pmm_alloc_pages(4);
  if (pages != 0 && (pages % PMM_PAGE_SIZE) == 0) { PASS(); }
  else { FAIL("alloc_pages failed"); }

  TEST("pmm_free_pages restores count");
  struct pmm_stats before, after;
  pmm_stats_get(&before);
  pmm_free_pages(pages, 4);
  pmm_stats_get(&after);
  if (after.free_pages == before.free_pages + 4) { PASS(); }
  else { FAIL("free count mismatch"); }
}

void test_pmm_is_free(void) {
  TEST("pmm_is_free after alloc");
  uint64_t page = pmm_alloc_page();
  if (page != 0 && pmm_is_free(page) == 0) { PASS(); }
  else { FAIL("page should not be free"); }

  TEST("pmm_is_free after free");
  pmm_free_page(page);
  if (pmm_is_free(page) == 1) { PASS(); }
  else { FAIL("page should be free"); }
}

int test_pmm_run(void) {
  printf("[test_pmm]\n");
  tests_run = 0;
  tests_passed = 0;
  test_pmm_init();
  test_pmm_alloc_free();
  test_pmm_alloc_pages();
  test_pmm_is_free();
  printf("  %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
