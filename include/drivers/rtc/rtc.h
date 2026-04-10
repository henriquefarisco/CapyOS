#ifndef DRIVERS_RTC_RTC_H
#define DRIVERS_RTC_RTC_H

#include <stdint.h>

struct rtc_time {
  uint8_t second;
  uint8_t minute;
  uint8_t hour;
  uint8_t day;
  uint8_t month;
  uint16_t year;
  uint8_t weekday;
};

void rtc_init(void);
void rtc_read(struct rtc_time *out);
uint64_t rtc_unix_timestamp(void);
void rtc_format_time(const struct rtc_time *t, char *buf, int buf_size);
void rtc_format_date(const struct rtc_time *t, char *buf, int buf_size);

#endif /* DRIVERS_RTC_RTC_H */
