#include <intrin.h>
#include "kernel.h"

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT 0x71
#define CMOS_SECONDS 0x00
#define CMOS_MINUTES 0x02
#define CMOS_HOURS 0x04
#define CMOS_DAY 0x07
#define CMOS_MONTH 0x08
#define CMOS_YEAR 0x09
#define CMOS_STATUS_B 0x0B
#define CMOS_CENTURY 0x32
#define CMOS_UPDATE_IN_PROGRESS 0x80
#define CMOS_BINARY_MODE 0x04
#define CMOS_24_HOUR_MODE 0x02
#define CMOS_PM_FLAG 0x80

typedef struct
{
    unsigned char second;
    unsigned char minute;
    unsigned char hour;
    unsigned char day;
    unsigned char month;
    unsigned char year;
    unsigned char century;
    unsigned char status;
} RtcSnapshot;

static unsigned char rtc_read_register(unsigned char index)
{
    __outbyte(CMOS_INDEX_PORT, index);
    return __inbyte(CMOS_DATA_PORT);
}

static int rtc_wait_for_update(void)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 1000000; ++attempt)
    {
        if ((rtc_read_register(0x0A) & CMOS_UPDATE_IN_PROGRESS) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int rtc_read_snapshot(RtcSnapshot *snapshot)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 3; ++attempt)
    {
        unsigned char second_after;

        if (!rtc_wait_for_update())
        {
            return 0;
        }

        snapshot->second = rtc_read_register(CMOS_SECONDS);
        snapshot->minute = rtc_read_register(CMOS_MINUTES);
        snapshot->hour = rtc_read_register(CMOS_HOURS);
        snapshot->day = rtc_read_register(CMOS_DAY);
        snapshot->month = rtc_read_register(CMOS_MONTH);
        snapshot->year = rtc_read_register(CMOS_YEAR);
        snapshot->century = rtc_read_register(CMOS_CENTURY);
        snapshot->status = rtc_read_register(CMOS_STATUS_B);
        second_after = rtc_read_register(CMOS_SECONDS);

        if (snapshot->second == second_after &&
            (rtc_read_register(0x0A) & CMOS_UPDATE_IN_PROGRESS) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static unsigned char rtc_decode_value(
    unsigned char value,
    unsigned char binary_mode)
{
    if (binary_mode)
    {
        return value;
    }
    return (unsigned char)((value >> 4) * 10 + (value & 0x0F));
}

static void terminal_write_two_digits(unsigned int value)
{
    if (value < 10)
    {
        terminal_write_character('0');
    }
    terminal_write_unsigned(value);
}

void clock_write_time(void)
{
    RtcSnapshot snapshot;
    unsigned char binary_mode;
    unsigned char hour_24;
    unsigned int year;

    if (!rtc_read_snapshot(&snapshot))
    {
        terminal_write("time: RTC did not provide a stable reading.\n");
        return;
    }

    binary_mode = (snapshot.status & CMOS_BINARY_MODE) != 0;
    hour_24 = snapshot.hour & (unsigned char)~CMOS_PM_FLAG;
    snapshot.second = rtc_decode_value(snapshot.second, binary_mode);
    snapshot.minute = rtc_decode_value(snapshot.minute, binary_mode);
    hour_24 = rtc_decode_value(hour_24, binary_mode);
    snapshot.day = rtc_decode_value(snapshot.day, binary_mode);
    snapshot.month = rtc_decode_value(snapshot.month, binary_mode);
    snapshot.year = rtc_decode_value(snapshot.year, binary_mode);
    snapshot.century = rtc_decode_value(snapshot.century, binary_mode);

    if ((snapshot.status & CMOS_24_HOUR_MODE) == 0)
    {
        if ((snapshot.hour & CMOS_PM_FLAG) != 0)
        {
            hour_24 = (unsigned char)(hour_24 % 12 + 12);
        }
        else
        {
            hour_24 %= 12;
        }
    }

    year = snapshot.century == 0
        ? 2000 + snapshot.year
        : snapshot.century * 100 + snapshot.year;

    terminal_write_unsigned(year);
    terminal_write_character('-');
    terminal_write_two_digits(snapshot.month);
    terminal_write_character('-');
    terminal_write_two_digits(snapshot.day);
    terminal_write_character(' ');
    terminal_write_two_digits(hour_24);
    terminal_write_character(':');
    terminal_write_two_digits(snapshot.minute);
    terminal_write_character(':');
    terminal_write_two_digits(snapshot.second);
    terminal_write(" UTC\n");
}
