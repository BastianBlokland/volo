#include "core_diag.h"
#include "core_time.h"

static void test_time_steady_clock() { diag_assert(time_steady_clock() > 0); }

static void test_time_weekday() {
  const TimeReal e = time_epoch;

  diag_assert(time_real_to_weekday(e) == TimeWeekDay_Thursday);
  diag_assert(time_real_to_weekday(time_real_offset(e, time_day)) == TimeWeekDay_Friday);
  diag_assert(time_real_to_weekday(time_real_offset(e, time_days(2))) == TimeWeekDay_Saturday);
  diag_assert(time_real_to_weekday(time_real_offset(e, time_days(3))) == TimeWeekDay_Sunday);
  diag_assert(time_real_to_weekday(time_real_offset(e, time_days(4))) == TimeWeekDay_Monday);
  diag_assert(time_real_to_weekday(time_real_offset(e, time_days(5))) == TimeWeekDay_Tuesday);
  diag_assert(time_real_to_weekday(time_real_offset(e, time_days(6))) == TimeWeekDay_Wednesday);
  diag_assert(time_real_to_weekday(time_real_offset(e, time_days(7))) == TimeWeekDay_Thursday);
}

static void test_time_real_to_date() {
  const TimeDate epochDate = time_real_to_date(time_epoch);
  diag_assert(epochDate.year == 1970);
  diag_assert(epochDate.month == TimeMonth_January);
  diag_assert(epochDate.day == 1);

  const TimeDate testDate1 = time_real_to_date(time_real_offset(time_epoch, time_days(42424)));
  diag_assert(testDate1.year == 2086);
  diag_assert(testDate1.month == TimeMonth_February);
  diag_assert(testDate1.day == 25);

  const TimeDate testDate2 = time_real_to_date(time_real_offset(time_epoch, -time_days(42424)));
  diag_assert(testDate2.year == 1853);
  diag_assert(testDate2.month == TimeMonth_November);
  diag_assert(testDate2.day == 6);
}

static void test_time_date_to_real() {
  const TimeReal dateTime =
      time_date_to_real((TimeDate){.year = 2021, .month = TimeMonth_June, .day = 1});
  const TimeReal fourthTwoDaysFromDateTime = time_real_offset(dateTime, time_days(42));

  diag_assert(
      fourthTwoDaysFromDateTime ==
      time_date_to_real((TimeDate){.year = 2021, .month = TimeMonth_July, .day = 13}));
}

static void test_time_real_clock() {
  const TimeDate today = time_real_to_date(time_real_clock());
  // If this code ever runs after 2200 it would be amazing, but i wont be alive to see it.
  diag_assert(today.year >= 2021 && today.year < 2200);
}

void test_time() {
  test_time_steady_clock();
  test_time_weekday();
  test_time_real_to_date();
  test_time_date_to_real();
  test_time_real_clock();
}
