#include "check_spec.h"
#include "core_time.h"

spec(time) {

  it("can compute the day-of-the-week from a real-time") {
    const TimeReal e = time_real_epoch;

    check_eq_int(time_real_to_weekday(e), TimeWeekDay_Thursday);
    check_eq_int(time_real_to_weekday(time_real_offset(e, time_day)), TimeWeekDay_Friday);
    check_eq_int(time_real_to_weekday(time_real_offset(e, time_days(2))), TimeWeekDay_Saturday);
    check_eq_int(time_real_to_weekday(time_real_offset(e, time_days(3))), TimeWeekDay_Sunday);
    check_eq_int(time_real_to_weekday(time_real_offset(e, time_days(4))), TimeWeekDay_Monday);
    check_eq_int(time_real_to_weekday(time_real_offset(e, time_days(5))), TimeWeekDay_Tuesday);
    check_eq_int(time_real_to_weekday(time_real_offset(e, time_days(6))), TimeWeekDay_Wednesday);
    check_eq_int(time_real_to_weekday(time_real_offset(e, time_days(7))), TimeWeekDay_Thursday);
  }

  it("can compute the date from a real-time") {
    const TimeDate epochDate = time_real_to_date(time_real_epoch);
    check_eq_int(epochDate.year, 1970);
    check_eq_int(epochDate.month, TimeMonth_January);
    check_eq_int(epochDate.day, 1);

    const TimeDate testDate1 =
        time_real_to_date(time_real_offset(time_real_epoch, time_days(42424)));
    check_eq_int(testDate1.year, 2086);
    check_eq_int(testDate1.month, TimeMonth_February);
    check_eq_int(testDate1.day, 25);

    const TimeDate testDate2 =
        time_real_to_date(time_real_offset(time_real_epoch, -time_days(42424)));
    check_eq_int(testDate2.year, 1853);
    check_eq_int(testDate2.month, TimeMonth_November);
    check_eq_int(testDate2.day, 6);

    const TimeDate testDate3 = time_real_to_date(time_real_offset(time_real_epoch, time_days(13)));
    check_eq_int(testDate3.year, 1970);
    check_eq_int(testDate3.month, TimeMonth_January);
    check_eq_int(testDate3.day, 14);
  }

  it("can compute the real-time for a date") {
    const TimeReal dateTime =
        time_date_to_real((TimeDate){.year = 2021, .month = TimeMonth_June, .day = 1});
    const TimeReal fourthTwoDaysFromDateTime = time_real_offset(dateTime, time_days(42));

    check_eq_int(
        fourthTwoDaysFromDateTime,
        time_date_to_real((TimeDate){.year = 2021, .month = TimeMonth_July, .day = 13}));
  }

  it("can retrieve the current real-time from the real-clock") {
    const TimeDate today = time_real_to_date(time_real_clock());
    // If this code ever runs after 2200 it would be amazing, but i wont be alive to see it.
    check(today.year >= 2021 && today.year < 2200);
  }

  it("can compute the time-duration between two dates") {
    const TimeDate     a       = (TimeDate){.year = 1700, .month = TimeMonth_April, .day = 13};
    const TimeDate     b       = (TimeDate){.year = 1992, .month = TimeMonth_June, .day = 9};
    const TimeDuration dur     = time_real_duration(time_date_to_real(a), time_date_to_real(b));
    const i32          durDays = dur / time_day;
    check_eq_int(durDays, 106708);
  }

  it("can compute the time-duration for dates below year 0") {
    const TimeDate     a       = (TimeDate){.year = -84, .month = TimeMonth_June, .day = 9};
    const TimeDate     b       = (TimeDate){.year = -42, .month = TimeMonth_April, .day = 13};
    const TimeDuration dur     = time_real_duration(time_date_to_real(a), time_date_to_real(b));
    const i32          durDays = dur / time_day;
    check_eq_int(durDays, 15283);
  }
}
