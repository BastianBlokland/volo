#include "core_diag.h"
#include "core_time.h"

static void test_time_steady_clock() { diag_assert(time_steady_clock() > 0); }

static void test_time_weekday() {
  const TimeReal e = time_epoch;

  diag_assert(time_real_weekday(e) == TimeWeekDay_Thursday);
  diag_assert(time_real_weekday(time_real_offset(e, time_day)) == TimeWeekDay_Friday);
  diag_assert(time_real_weekday(time_real_offset(e, time_days(2))) == TimeWeekDay_Saturday);
  diag_assert(time_real_weekday(time_real_offset(e, time_days(3))) == TimeWeekDay_Sunday);
  diag_assert(time_real_weekday(time_real_offset(e, time_days(4))) == TimeWeekDay_Monday);
  diag_assert(time_real_weekday(time_real_offset(e, time_days(5))) == TimeWeekDay_Tuesday);
  diag_assert(time_real_weekday(time_real_offset(e, time_days(6))) == TimeWeekDay_Wednesday);
  diag_assert(time_real_weekday(time_real_offset(e, time_days(7))) == TimeWeekDay_Thursday);
}

void test_time() {
  test_time_steady_clock();
  test_time_weekday();
}
