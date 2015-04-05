#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <linux/i2c-dev.h>


/* DS1307 Definations */
/* Global */
#define DS1307_DEVAD     (0x68)

/* Second */
#define DS1307_REGAD_SEC (0x00)
#define DS1307_HALT      (1 << 7)

/* Minute */
#define DS1307_REGAD_MIN (0x01)

/* Hour */
#define DS1307_REGAD_HRS (0x02)
#define DS1307_12H_MODE  (1 << 6)
#define DS1307_12H_PM    (1 << 5)

/* Weekday */
#define DS1307_REGAD_DOW (0x03)
/******************************************************************************
 * DOW values are user-defined, all sequential definations should work.
 * However, POR will put registers into 01/01/00 01 00:00:00,
 * which is a Saturday.
 *****************************************************************************/
#define DS1307_DOW_SAT   (0x01)
#define DS1307_DOW_SUN   (0x02)
#define DS1307_DOW_MON   (0x03)
#define DS1307_DOW_TUE   (0x04)
#define DS1307_DOW_WED   (0x05)
#define DS1307_DOW_THU   (0x06)
#define DS1307_DOW_FRI   (0x07)

/* Date */
#define DS1307_REGAD_DAY (0x04)
#define DS1307_REGAD_MON (0x05)
#define DS1307_REGAD_YRS (0x06)

/* Control */
#define DS1307_REGAD_CTL (0x07)
#define DS1307_SQW_OUT   (1 << 7)
#define DS1307_SQW_EN    (1 << 4)
#define DS1307_SQW_RS1   (1 << 1)
#define DS1307_SQW_RS0   (1 << 0)
/******************************************************************************
 * Square wave output
 * ----------------------
 * OUT EN RS1 RS0 Result
 *  X  1   0   0      1Hz
 *  X  1   0   1   4096Hz
 *  X  1   1   0   8192Hz
 *  X  1   1   1  32768Hz
 *  1  0   X   X     High
 *  0  0   X   X      Low
 *****************************************************************************/
#define DS1307_SQW_1HZ   (0x00 | DS1307_SQW_EN)
#define DS1307_SQW_4KHZ  (0x00 | DS1307_SQW_EN | DS1307_SQW_RS0)
#define DS1307_SQW_8KHZ  (0x00 | DS1307_SQW_EN | DS1307_SQW_RS1)
#define DS1307_SQW_32KHZ (0x00 | DS1307_SQW_EN | DS1307_SQW_RS0 | DS1307_SQW_RS1)
#define DS1307_SQW_H     (0x00 | DS1307_SQW_OUT)
#define DS1307_SQW_L     (0x00)

/* RAM */
#define DS1307_REGAD_RAM (0x08)
#define DS1307_REGAD_END (0x40)

/* Helper functions */

int i2c_open(int bus) {
  const int fn_len = 20;
  char fn[fn_len];
  int res, file;

  if (bus < 0) {
    return -EINVAL;
  }

  /* Open i2c-dev file */
  snprintf(fn, fn_len, "/dev/i2c-%d", bus);
  if ((file = open(fn, O_RDWR)) < 0) {
    perror("open() failed (make sure i2c_dev is loaded and you have the permission)");
    return file;
  }

  /* Query functions */
  unsigned long funcs;
  if ((res = ioctl(file, I2C_FUNCS, &funcs)) < 0) {
    perror("ioctl() I2C_FUNCS failed");
    return res;
  }
  fprintf(stdout, "Device: %s (", fn);
  if (funcs & I2C_FUNC_I2C) {
    fputs("I2C_FUNC_I2C ", stdout);
  }
  if (funcs & I2C_FUNC_SMBUS_BYTE) {
    fputs("I2C_FUNC_SMBUS_BYTE ", stdout);
  }
  fputs("\b)\n", stdout);
  fflush(stdout);

  return file;
}

int i2c_select(int file, int addr) {
  /* addr in [0x00, 0x7f] */
  int res;

  if ((res = ioctl(file, I2C_SLAVE, addr)) < 0) {
    perror("ioctl() I2C_SLAVE failed");
  }

  return res;
}

int i2c_read_byte(int file, uint8_t reg_addr, uint8_t *data) {
  if (NULL == data) {
    return -EFAULT;
  }

  int res;
  if ((res = write(file, &reg_addr, 1)) < 0) {
    perror("write() register address failed");
    return res;
  }

  if ((res = read(file, data, 1)) < 0) {
    perror("read() data failed");
    return res;
  }

  return 0;
}

int i2c_write_byte(int file, uint8_t reg_addr, uint8_t data) {
  /* must concatenate address and data,                         *
   * otherwise transfer will be terminated before data is sent. */
  int res;
  uint8_t buf[2] = {reg_addr, data};

  if ((res = write(file, buf, 2)) < 0) {
    perror("write() register address / data failed");
    return res;
  }

  return 0;
}

int weekday2c(uint8_t wkd, const char **c) {
  if (NULL == c) {
    return -EFAULT;
  }

  switch (wkd) {
    case DS1307_DOW_SAT: {
      *c = "Saturday";
      return 0;
    }
    case DS1307_DOW_SUN: {
      *c = "Sunday";
      return 0;
    }
    case DS1307_DOW_MON: {
      *c = "Monday";
      return 0;
    }
    case DS1307_DOW_TUE: {
      *c = "Tuesday";
      return 0;
    }
    case DS1307_DOW_WED: {
      *c = "Wednesday";
      return 0;
    }
    case DS1307_DOW_THU: {
      *c = "Thursday";
      return 0;
    }
    case DS1307_DOW_FRI: {
      *c = "Friday";
      return 0;
    }
    default: {
      *c = "???";
      return -EINVAL;
    }
  }
}

bool isbcd(uint8_t i) {
  int h = i >> 4;
  int l = i & 0x0f;

  if ((h < 10) || (l < 10)) {
    return true;
  } else {
    return false;
  }
}

uint8_t bcd2i(uint8_t bcd) {
  /* This function does NOT validate BCD. */
  return (bcd >> 4) * 10 + (bcd & 0x0f);
}

uint8_t i2bcd(uint8_t i) {
  /* This function does NOT validate range. */
  return ((i / 10) << 4) | (i % 10);
}

int ds1307_print_time(int file) {
  int res;

  uint8_t sec;
  bool hlt;
  if ((res = i2c_read_byte(file, DS1307_REGAD_SEC, &sec)) < 0) {
    return res;
  }
  hlt = (sec & DS1307_HALT) ? true : false;
  sec = bcd2i(sec & (~DS1307_HALT));

  uint8_t min;
  if ((res = i2c_read_byte(file, DS1307_REGAD_MIN, &min)) < 0) {
    return res;
  }
  min = bcd2i(min);

  uint8_t hrs;
  bool h12;
  bool hpm;
  if ((res = i2c_read_byte(file, DS1307_REGAD_HRS, &hrs)) < 0) {
    return res;
  }
  h12 = (hrs & DS1307_12H_MODE) ? true : false;
  hpm = (hrs & DS1307_12H_PM  ) ? true : false;
  if (h12) {
    hrs = bcd2i(hrs & (~(DS1307_12H_MODE | DS1307_12H_PM)));
  } else {
    hrs = bcd2i(hrs & (~DS1307_12H_MODE));
  }

  uint8_t dow;
  const char *dows;
  if ((res = i2c_read_byte(file, DS1307_REGAD_DOW, &dow)) < 0) {
    return res;
  }
  if ((res = weekday2c(dow, &dows)) < 0) {
    return res;
  }

  uint8_t day;
  if ((res = i2c_read_byte(file, DS1307_REGAD_DAY, &day)) < 0) {
    return res;
  }
  day = bcd2i(day);

  uint8_t mon;
  if ((res = i2c_read_byte(file, DS1307_REGAD_MON, &mon)) < 0) {
    return res;
  }
  mon = bcd2i(mon);

  uint8_t yrs;
  if ((res = i2c_read_byte(file, DS1307_REGAD_YRS, &yrs)) < 0) {
    return res;
  }
  yrs = bcd2i(yrs);

  uint8_t ctl;
  if ((res = i2c_read_byte(file, DS1307_REGAD_CTL, &ctl)) < 0) {
    return res;
  }
  if (h12) {
    fprintf(stdout, "20%02d-%02d-%02d %s %s %02d:%02d:%02d %s 12H\n", yrs, mon, day, dows, hpm ? "PM" : "AM", hrs, min, sec, hlt ? "HALTED" : "RUNNING");
  } else {
    fprintf(stdout, "20%02d-%02d-%02d %s    %02d:%02d:%02d %s 24H\n", yrs, mon, day, dows, hrs, min, sec, hlt ? "HALTED" : "RUNNING");
  }

  return 0;
}

int ds1307_halt(int file, bool halt) {
  /* Timing is not critical here, halting is envolved anyway... */

  int res;
  uint8_t sec;

  if ((res = i2c_read_byte(file, DS1307_REGAD_SEC, &sec)) < 0) {
    return res;
  }

  if (halt) {
    sec |= DS1307_HALT;
  } else {
    if (false == (sec & DS1307_HALT)) {
      fprintf(stdout, "Halt bit is already cleared (0x%02x)\n", sec);
      return 0;
    }
    sec &= (~DS1307_HALT);
  }

  if ((res = i2c_write_byte(file, DS1307_REGAD_SEC, sec)) < 0) {
    return res;
  }

  fprintf(stdout, "Halt bit %s (0x%02x)\n", halt ? "set" : "cleared", sec);

  return 0;
}

int ds1307_sanity_check(int file, bool *ok) {
  int res;
  uint8_t reg;

  if (NULL == ok) {
    return -EFAULT;
  }

  /* Second */
  if ((res = i2c_read_byte(file, DS1307_REGAD_SEC, &reg)) < 0) {
    return res;
  }
  reg &= (~DS1307_HALT);
  if ((!isbcd(reg)) || (bcd2i(reg) > 59)) {
    *ok = false;
    return 0;
  }

  /* Minute */
  if ((res = i2c_read_byte(file, DS1307_REGAD_MIN, &reg)) < 0) {
    return res;
  }
  if ((!isbcd(reg)) || (bcd2i(reg) > 59)) {
    *ok = false;
    return 0;
  }

  /* Hour */
  if ((res = i2c_read_byte(file, DS1307_REGAD_HRS, &reg)) < 0) {
    return res;
  }
  bool h12 = (reg & DS1307_12H_MODE) ? true : false;
  reg &= (~DS1307_12H_MODE);
  if (h12) {
    reg &= (~DS1307_12H_PM);
    if ((!isbcd(reg)) || (bcd2i(reg) > 12) || (0 == bcd2i(reg))) {
      *ok = false;
      return 0;
    }
  } else {
    if ((!isbcd(reg)) || (bcd2i(reg) > 23)) {
      *ok = false;
      return 0;
    }
  }

  /* Day of Week */
  if ((res = i2c_read_byte(file, DS1307_REGAD_DOW, &reg)) < 0) {
    return res;
  }
  if ((!isbcd(reg)) || (bcd2i(reg) > 7) || (0 == bcd2i(reg))) {
    *ok = false;
    return 0;
  }

  /* Day */
  if ((res = i2c_read_byte(file, DS1307_REGAD_DAY, &reg)) < 0) {
    return res;
  }
  if ((!isbcd(reg)) || (bcd2i(reg) > 31) || (0 == bcd2i(reg))) {
    *ok = false;
    return 0;
  }

  /* Month */
  if ((res = i2c_read_byte(file, DS1307_REGAD_MON, &reg)) < 0) {
    return res;
  }
  if ((!isbcd(reg)) || (bcd2i(reg) > 12) || (0 == bcd2i(reg))) {
    *ok = false;
    return 0;
  }

  /* Year */
  if ((res = i2c_read_byte(file, DS1307_REGAD_YRS, &reg)) < 0) {
    return res;
  }
  if (!isbcd(reg)) {
    *ok = false;
    return 0;
  }
  /* TODO: YMD cross validation with leap-year awareness */

  /* Control */
  if ((res = i2c_read_byte(file, DS1307_REGAD_YRS, &reg)) < 0) {
    return res;
  }
  reg &= (~(DS1307_SQW_OUT | DS1307_SQW_EN | DS1307_SQW_RS1 | DS1307_SQW_RS0));
  if (0 != reg) {
    *ok = false;
    return 0;
  }

  /* All pass */
  *ok = true;
  return 0;
}

int ds1307_set_hfmt(int file, bool h12) {
  /* Set 12H/24H */
  /* TODO: wait if time is 59:59 and is not halted, so we do not cause glitch. */

  int res;
  uint8_t hrs;
  bool oldh12;

  if ((res = i2c_read_byte(file, DS1307_REGAD_HRS, &hrs)) < 0) {
    return res;
  }
  oldh12 = (hrs & DS1307_12H_MODE) ? true : false;
  hrs &= (~DS1307_12H_MODE);

  if (h12) {
    if (oldh12) {
      fputs("Hour format is already set to 12H\n", stdout);
      return 0;
    } else {
      bool pm = false;
      hrs = bcd2i(hrs);
      if (hrs > 12) {
        hrs -= 12;
        pm = true;
      }
      if (12 == hrs) {
        pm = true;
      }
      if (0 == hrs) {
        /* 00:00 = 12:00 AM */
        hrs = 12;
      }
      hrs = i2bcd(hrs);
      hrs |= DS1307_12H_MODE;
      if (pm) {
        hrs |= DS1307_12H_PM;
      }
    }
  } else {
    if (!oldh12) {
      fputs("Hour format is already set to 24H\n", stdout);
      return 0;
    } else {
      bool pm = hrs & DS1307_12H_PM;
      hrs &= (~DS1307_12H_PM);
      hrs = bcd2i(hrs);
      if (pm) {
        hrs += 12;
        if (24 == hrs) {
          /* Was 12:00 PM, = 12:00 */
          hrs = 12;
        }
      } else {
        if (12 == hrs) {
          /* 12:00 AM = 0:00 */
          hrs = 0;
        }
      }
      hrs = i2bcd(hrs);
    }
  }

  if ((res = i2c_write_byte(file, DS1307_REGAD_HRS, hrs)) < 0) {
    return res;
  }

  fprintf(stdout, "Hour format set to %s (0x%02x)\n", h12 ? "12H" : "24H", hrs);

  return 0;
}

int ds1307_dump(int file, uint8_t start, uint8_t count) {
  /* Intended for internal use only. */

  int i, res;
  uint8_t reg;

  if ((start + count - 1) > 0x3f) {
    fprintf(stderr, "BUG: end address 0x%02x is out of range!", start + count - 1);
    return -EINVAL;
  }

  for (i = 0; i < count; i ++) {
    if ((res = i2c_read_byte(file, start + i, &reg)) < 0) {
      return res;
    }
    fprintf(stdout, "Register @ 0x%02x: 0x%02x\n", start + i, reg);
  }

  return 0;
}

int ds1307_test_ram_byte(int file, uint8_t byte) {
  /* Intended for internal use only. */

  int ad;
  int res;

  /* Write */
  for (ad = DS1307_REGAD_RAM; ad < DS1307_REGAD_END; ad ++) {
    if ((res = i2c_write_byte(file, ad, byte)) < 0) {
      return res;
    }
  }

  /* Read and compare */
  uint8_t reg;
  for (ad = DS1307_REGAD_RAM; ad < DS1307_REGAD_END; ad ++) {
    if ((res = i2c_read_byte(file, ad, &reg)) < 0) {
      return res;
    }
    if (reg != byte) {
      fprintf(stdout, "Register @ 0x%02x is bad: expect 0x%02x, got 0x%02x\n", ad, byte, reg);
      /* This is not a fault error (for the program), so continue to check other registers. */
    }
  }

  return 0;
}

int ds1307_test_ram(int file) {
  int res;
  int bit;

  /* Walk 1 */
  for (bit = 0; bit < 8; bit ++) {
    if ((res = ds1307_test_ram_byte(file, 1 << bit)) < 0) {
      return res;
    }
    fprintf(stdout, "Done checking 0x%02x\n", 1 << bit);
  }

  /* 0x55, 0xaa, 0x00 and 0xff */
  if ((res = ds1307_test_ram_byte(file, 0x55)) < 0) {
    return res;
  }
  fprintf(stdout, "Done checking 0x%02x\n", 0x55);
  if ((res = ds1307_test_ram_byte(file, 0xaa)) < 0) {
    return res;
  }
  fprintf(stdout, "Done checking 0x%02x\n", 0xaa);
  if ((res = ds1307_test_ram_byte(file, 0xff)) < 0) {
    return res;
  }
  fprintf(stdout, "Done checking 0x%02x\n", 0xff);
  if ((res = ds1307_test_ram_byte(file, 0x00)) < 0) {
    return res;
  }
  fprintf(stdout, "Done checking 0x%02x\n", 0x00);

  return 0;
}

int ds1307_get_sqw(int file) {
  int res;
  uint8_t reg;
  const char *freq[] = {"1", "4096", "8192", "32768"};

  if ((res = i2c_read_byte(file, DS1307_REGAD_CTL, &reg)) < 0) {
    return res;
  }

  if (reg & DS1307_SQW_EN) {
    int i = reg & (DS1307_SQW_RS1 | DS1307_SQW_RS0);
    fprintf(stdout, "Square wave output is %sHz\n", freq[i]);
  } else {
    fprintf(stdout, "Square wave output is constantly %s\n", (reg & DS1307_SQW_OUT) ? "HIGH" : "LOW");
  }

  return 0;
}

int ds1307_set_sqw(int file, int hz) {
  /* HZ: 0 = LOW 1 = HIGH 2 = 1Hz 3 = 4kHz 4 = 8kHz 5 = 32kHz */

  int res;
  const uint8_t reg_table[] = {DS1307_SQW_L, DS1307_SQW_H, DS1307_SQW_1HZ, DS1307_SQW_4KHZ, DS1307_SQW_8KHZ, DS1307_SQW_32KHZ};

  if ((hz < 0) || (hz > 5)) {
    fprintf(stderr, "Invalid sqaure wave setting number `%d'!\n", hz);
    return -EINVAL;
  }

  if ((res = i2c_write_byte(file, DS1307_REGAD_CTL, reg_table[hz])) < 0) {
    return res;
  }

  /* User feedback */
  ds1307_get_sqw(file);
  return 0;
}

int ds1307_sync_time(int file) {
  /* Set time to the system time (losely). */
  int res;
  uint8_t reg;
  bool h12, halt;
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  /* Read previous settings */
  if ((res = i2c_read_byte(file, DS1307_REGAD_SEC, &reg)) < 0) {
    return res;
  }
  halt = (reg & DS1307_HALT) ? true : false;
  if ((res = i2c_read_byte(file, DS1307_REGAD_HRS, &reg)) < 0) {
    return res;
  }
  h12 = (reg & DS1307_12H_MODE) ? true : false;

  /* Set date */
  if ((res = i2c_write_byte(file, DS1307_REGAD_YRS, i2bcd(tm.tm_year + 1900 - 2000))) < 0) {
    return res;
  }
  if ((res = i2c_write_byte(file, DS1307_REGAD_MON, i2bcd(tm.tm_mon + 1))) < 0) {
    return res;
  }
  if ((res = i2c_write_byte(file, DS1307_REGAD_DAY, i2bcd(tm.tm_mday))) < 0) {
    return res;
  }
  /* Day of week starts from 0 = Sunday... */
  const uint8_t dow_table[] = {2, 3, 4, 5, 6, 7, 1};
  if ((res = i2c_write_byte(file, DS1307_REGAD_DAY, dow_table[tm.tm_mday])) < 0) {
    return res;
  }

  /* Set time */
  /* Halt while setting second. TODO: handle leap second (tm.tm_sec can be 60 and 61)! */
  if ((res = i2c_write_byte(file, DS1307_REGAD_SEC, DS1307_HALT | i2bcd(tm.tm_sec))) < 0) {
    return res;
  }
  if ((res = i2c_write_byte(file, DS1307_REGAD_MIN, i2bcd(tm.tm_min))) < 0) {
    return res;
  }
  /* Always set 24H time first, then set mode to 12H if necessary */
  if ((res = i2c_write_byte(file, DS1307_REGAD_HRS, i2bcd(tm.tm_hour))) < 0) {
    return res;
  }

  if (h12) {
    ds1307_set_hfmt(file, h12);
  }
  if (!halt) {
    ds1307_halt(file, halt);
  }

  return 0;
}

/******************************************************************************
 * Option list (operations will be carried out in argument list order):
 * 1 - set 12H format
 * 2 - set 24H format
 * a <int> - override address
 * b <int> - set bus number (must be done prior to any other operation)
 * c - chip sanity check
 * d - dump RAM
 * D - dump everything
 * g - get SQW settings
 * h - clear halt bit
 * H - set halt bit
 * p - print date / time
 * s - set SQW settings
 * S - set date
 * t - test ram
 *****************************************************************************/

void print_help(const char *self) {
  fprintf(stderr, "\
  Usage:\n\
    %s -b <bus number> [list of operations]\n\
  \n\
  Operations will be carried out in argument list order.\n\
  Bus number and address can be overrided in the middle of the list.\n\
  \n\
  List of operations:\n\
    -1      : set 12-hour format.\n\
    -2      : set 24-hour format.\n\
    -a <int>: override device address (default: 0x%02x, in range 0x03 to 0x7f).\n\
              NOTE: this value will NOT be reset to default after switching\n\
                    bus.\n\
              WARN: use this option only when you know what you are doing!\n\
    -b <int>: set bus number (must be set prior to any operations).\n\
              NOTE: you can use `i2cdetect -l' to list I2C buses present in the\n\
                    system.\n\
    -c      : chip sanity check.\n\
    -d      : dump on-chip NV SRAM.\n\
              NOTE: it is normal for some bits to be 1 after power-on-reset.\n\
    -D      : dump all registers, for debugging.\n\
    -g      : get current square wave output settings.\n\
    -h      : clear halt bit (start the clock).\n\
    -H      : set halt bit (pause the clock).\n\
    -p      : print current date and time in the device.\n\
    -s <int>: set square wave output settings:\n\
                0 = constantly low;\n\
                1 = constantly high;\n\
                2 =     1Hz;\n\
                3 =  4096Hz;\n\
                4 =  8192Hz;\n\
                5 = 32768Hz.\n\
    -S      : synchronize chip time to system time.\n\
              NOTE: 12/24-hour mode and halting will be perserved.\n\
    -t      : test on-chip NV SRAM.\n\
              NOTE: The chip may go offline during the process, you will need\n\
                    to reset the chip manually. Suggest halting the clock\n\
                    before checking to avoid possible hardware bugs.\n\
  \n\
  Example:\n\
    Print date and time in the DS1307 on i2c-1:\n\
      %s -b 1 -h -p\n\
  \n", self, DS1307_DEVAD, self);
}

void handle_bad_opts(void) {
  if ((optopt == 'a') || (optopt == 'b') || (optopt == 's')) {
    fprintf(stderr, "ERROR: option -%c requires an argument.\n\n", optopt);
  } else if (isprint(optopt)) {
    fprintf(stderr, "ERROR: unknown option `-%c'.\n\n", optopt);
  } else {
    fprintf(stderr, "ERROR: unknown option character `\\x%x'.\n\n", optopt);
  }
}

int read_int(const char *s) {
  /* convert a base 8 / 10 / 16 number in string into integer */
  int i = -EIO;

  if (NULL == s) {
    return -EFAULT;
  }

  if ('0' == s[0]) {
    if (('x' == s[1]) || ('X' == s[1])) {
      /* Hex */
      if (sscanf(&s[2], "%x", &i) != 1) {
        return -EINVAL;
      }
    } else {
      /* Oct */
      if (sscanf(s, "%o", &i) != 1) {
        return -EINVAL;
      }
    }
  } else {
    /* Dec */
    if (sscanf(s, "%d", &i) != 1) {
      return -EINVAL;
    }
  }

  return i;
}

int main(int argc, char *argv[]) {
  int file = -1;
  int res;

  if (argc < 2) {
    print_help(argv[0]);
    return 0;
  }

  int c;
  int ad = DS1307_DEVAD;
  opterr = 0;
  while ((c = getopt(argc, argv, "12a:b:cdDghHps:St")) != -1) {
    switch (c) {
      case '1':
      case '2': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = ds1307_set_hfmt(file, '1' == c)) < 0) {
          close(file);
          return res;
        }
        break;
      }

      case 'a': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to address selection.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((ad = read_int(optarg)) < 0) {
          fprintf(stderr, "ERROR: invalid slave address `%s'.\n\n", optarg);
          print_help(argv[0]);
          return -EINVAL;
        }
        if ((ad < 0x03) || (ad > 0x7f)) {
          fprintf(stderr, "ERROR: invalid slave address `%s' (out of valid range of 0x03 to 0x7f).\n\n", optarg);
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = i2c_select(file, ad)) < 0) {
          close(file);
          return res;
        }

        fprintf(stdout, "Address set to 0x%02x\n", ad);
        break;
      }

      case 'b': {
        if (file >= 0) {
          /* We are switching to a new file, close the old one first */
          close(file);
          file = -1; /* So we do not double-close */
        }

        int bn;
        if ((bn = read_int(optarg)) < 0) {
          fprintf(stderr, "ERROR: invalid bus number `%s'.\n\n", optarg);
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((file = i2c_open(bn)) < 0) {
          return file;
        }

        if ((res = i2c_select(file, ad)) < 0) {
          close(file);
          return res;
        }

        fprintf(stdout, "Address set to 0x%02x\n", ad);
        break;
      }

      case 'c': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        bool ok;
        if ((res = ds1307_sanity_check(file, &ok)) < 0) {
          close(file);
          return res;
        }

        fprintf(stdout, "Sanity check: %s\n", ok ? "PASS" : "FAIL");
        break;
      }

      case 'd':
      case 'D': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        uint8_t start = ('d' == c) ? DS1307_REGAD_RAM : 0;
        uint8_t count = ('d' == c) ? (DS1307_REGAD_END - DS1307_REGAD_RAM) : DS1307_REGAD_END;
        if ((res = ds1307_dump(file, start, count)) < 0) {
          close(file);
          return res;
        }
        break;
      }

      case 'g': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = ds1307_get_sqw(file)) < 0) {
          close(file);
          return res;
        }
        break;
      }

      case 'h':
      case 'H': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = ds1307_halt(file, 'H' == c)) < 0) {
          close(file);
          return res;
        }
        break;
      }

      case 'p': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = ds1307_print_time(file)) < 0) {
          close(file);
          return res;
        }
        break;
      }

      case 's': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        int s;
        if ((s = read_int(optarg)) < 0) {
          fprintf(stderr, "ERROR: invalid square wave setting number `%s'.\n\n", optarg);
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = ds1307_set_sqw(file, s)) < 0) {
          close(file);
          return res;
        }
        break;
      }

      case 'S': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = ds1307_sync_time(file)) < 0) {
          close(file);
          return res;
        }
        break;
      }

      case 't': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = ds1307_test_ram(file)) < 0) {
          close(file);
          return res;
        }
        break;
      }

      case '?': {
        handle_bad_opts();
        return -EINVAL;
      }

      default: {
        fprintf(stderr, "BUG: switch fall-through on `%c'!\n", c);
        abort();
      }
    }
  }

  if (file >= 0) {
    close(file);
  }
  return 0;
}
