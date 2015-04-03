#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <linux/i2c-dev.h>

#define BUS_NO 1 /* use i2c-1 */

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

uint8_t bcd2i(uint8_t bcd) {
  /* This function does NOT validate BCD. */
  return (bcd >> 4) * 10 + (bcd & 0x0f);
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
    fprintf(stdout, "20%02d-%02d-%02d %s %s %02d:%02d:%02d %s\n", yrs, mon, day, dows, hpm ? "PM" : "AM", hrs, min, sec, hlt ? "HALTED" : "RUNNING");
  } else {
    fprintf(stdout, "20%02d-%02d-%02d %s %02d:%02d:%02d %s\n", yrs, mon, day, dows, hrs, min, sec, hlt ? "HALTED" : "RUNNING");
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
    sec &= (~DS1307_HALT);
  }

  if ((res = i2c_write_byte(file, DS1307_REGAD_SEC, sec)) < 0) {
    return res;
  }

  fprintf(stdout, "Halt bit %s (0x%02x)\n", halt ? "set" : "cleared", sec);

  return 0;
}

// memory test / sanity check
// dump ram

/******************************************************************************
 * Option list (operations will be carried out in argument list order):
 * a <int> - override address
 * b <int> - set bus number (must be done prior to any other operation)
 * p - print date / time
 * h - clear halt bit
 * H - set halt bit
 * TODO:
 * chip sanity check
 * dump ram
 * ram test
 * set date
 * sqw info
 * set sqw
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
    -a <int>: override device address (default: 0x%02x).\n\
              NOTE: this value will NOT be reset to default after switching bus.\n\
    -b <int>: set bus number (must be set prior to any operations).\n\
    -h      : clear halt bit (start the clock).\n\
    -H      : set halt bit (pause the clock).\n\
    -p      : print current date and time in the device.\n\
  \n\
  Example:\n\
    %s -b 1 -h -p\n\
  \n", self, DS1307_DEVAD, self);
}

void handle_bad_opts(void) {
  if ((optopt == 'a') || (optopt == 'b')) {
    fprintf(stderr, "ERROR: option -%c requires an argument.\n\n", optopt);
  } else if (isprint(optopt)) {
    fprintf(stderr, "ERROR: unknown option `-%c'.\n\n", optopt);
  } else {
    fprintf(stderr, "ERROR: unknown option character `\\x%x'.\n\n", optopt);
  }
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
  while ((c = getopt(argc, argv, "a:b:phH")) != -1) {
    switch (c) {
      case 'a': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to address selection.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = i2c_select(file, ad)) < 0) {
          return res;
        }

        break;
      }

      case 'b': {
        if (file >= 0) {
          /* We are switching to a new file, close the old one first */
          close(file);
          file = -1; /* So we do not double-close */
        }

        int bn;
        if ((res = sscanf(optarg, "%d", &bn)) == 1) {
          if ((file = i2c_open(bn)) < 0) {
            return file;
          }

          if ((res = i2c_select(file, ad)) < 0) {
            return res;
          }
        } else {
          fprintf(stderr, "ERROR: invalid bus number `%s'.\n\n", optarg);
          print_help(argv[0]);
          return -EINVAL;
        }

        break;
      }

      case 'p': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        ds1307_print_time(file);
        break;
      }

      case 'h': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        ds1307_halt(file, false);
        break;
      }

      case 'H': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        ds1307_halt(file, true);
        break;
      }

      case '?': {
        handle_bad_opts();
        return 1;
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
