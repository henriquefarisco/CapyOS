#include "drivers/rtc/rtc.h"
#include <stddef.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static inline void outb_rtc(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb_rtc(uint16_t port) {
  uint8_t ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static uint8_t cmos_read(uint8_t reg) {
  outb_rtc(CMOS_ADDR, reg);
  return inb_rtc(CMOS_DATA);
}

static int cmos_updating(void) {
  outb_rtc(CMOS_ADDR, 0x0A);
  return (inb_rtc(CMOS_DATA) & 0x80) ? 1 : 0;
}

static uint8_t bcd_to_bin(uint8_t bcd) {
  return (uint8_t)((bcd & 0x0F) + ((bcd >> 4) * 10));
}

void rtc_init(void) {
  /* RTC is always available on x86 via CMOS */
}

void rtc_read(struct rtc_time *out) {
  if (!out) return;

  while (cmos_updating()) {}

  uint8_t sec  = cmos_read(0x00);
  uint8_t min  = cmos_read(0x02);
  uint8_t hour = cmos_read(0x04);
  uint8_t day  = cmos_read(0x07);
  uint8_t mon  = cmos_read(0x08);
  uint8_t year = cmos_read(0x09);
  uint8_t century = cmos_read(0x32);
  uint8_t regB = cmos_read(0x0B);

  if (!(regB & 0x04)) {
    sec  = bcd_to_bin(sec);
    min  = bcd_to_bin(min);
    hour = bcd_to_bin(hour & 0x7F);
    day  = bcd_to_bin(day);
    mon  = bcd_to_bin(mon);
    year = bcd_to_bin(year);
    century = bcd_to_bin(century);
  }

  if (!(regB & 0x02) && (hour & 0x80)) {
    hour = (uint8_t)(((hour & 0x7F) + 12) % 24);
  }

  out->second = sec;
  out->minute = min;
  out->hour = hour;
  out->day = day;
  out->month = mon;
  out->year = (uint16_t)(century * 100 + year);
  out->weekday = 0;
}

uint64_t rtc_unix_timestamp(void) {
  struct rtc_time t;
  rtc_read(&t);

  uint32_t y = t.year;
  uint32_t m = t.month;
  uint32_t d = t.day;

  if (m <= 2) { y--; m += 12; }

  uint64_t days = 365ULL * y + y / 4 - y / 100 + y / 400 +
                  (153 * (m - 3) + 2) / 5 + d - 719469;
  return days * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}

void rtc_format_time(const struct rtc_time *t, char *buf, int buf_size) {
  if (!t || !buf || buf_size < 9) return;
  buf[0] = '0' + (t->hour / 10);
  buf[1] = '0' + (t->hour % 10);
  buf[2] = ':';
  buf[3] = '0' + (t->minute / 10);
  buf[4] = '0' + (t->minute % 10);
  buf[5] = ':';
  buf[6] = '0' + (t->second / 10);
  buf[7] = '0' + (t->second % 10);
  buf[8] = '\0';
}

void rtc_format_date(const struct rtc_time *t, char *buf, int buf_size) {
  if (!t || !buf || buf_size < 11) return;
  uint16_t y = t->year;
  buf[0] = '0' + (char)(y / 1000);
  buf[1] = '0' + (char)((y / 100) % 10);
  buf[2] = '0' + (char)((y / 10) % 10);
  buf[3] = '0' + (char)(y % 10);
  buf[4] = '-';
  buf[5] = '0' + (t->month / 10);
  buf[6] = '0' + (t->month % 10);
  buf[7] = '-';
  buf[8] = '0' + (t->day / 10);
  buf[9] = '0' + (t->day % 10);
  buf[10] = '\0';
}
