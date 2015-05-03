#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include <linux/i2c-dev.h>


/* TMP007 Definations */
/* Global */
#define TMP007_DEVAD_MIN  (0x40)
#define TMP007_DEVAD_MAX  (0x47)
#define TMP007_DEVAD_DEF  TMP007_DEVAD_MIN

/* Sensing */
#define TMP007_REG_VOLT   (0x00)
#define TMP007_REG_TDIE   (0x01)
#define TMP007_REG_TOBJ   (0x03)

/* Configuration */
#define TMP007_REG_CONFIG (0x02)
#define TMP007_REG_TOBJ_L (0x07)
#define TMP007_REG_TOBJ_H (0x06)
#define TMP007_REG_TDIE_L (0x09)
#define TMP007_REG_TDIE_H (0x08)

/* Corrections */
#define TMP007_REG_S0     (0x0a)
#define TMP007_REG_A0     (0x0b)
#define TMP007_REG_A1     (0x0c)
#define TMP007_REG_B0     (0x0d)
#define TMP007_REG_B1     (0x0e)
#define TMP007_REG_B2     (0x0f)
#define TMP007_REG_C      (0x10)
#define TMP007_REG_TC0    (0x11)
#define TMP007_REG_TC1    (0x12)

/* Status & Misc. */
#define TMP007_REG_STATUS (0x04)
#define TMP007_REG_STAMSK (0x05)
#define TMP007_REG_DEVID  (0x1f)
#define TMP007_REG_MEMIO  (0x2a)

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

int i2c_read_word(int file, uint8_t reg_addr, uint16_t *data) {
  if (NULL == data) {
    return -EFAULT;
  }

  int res;
  uint8_t tmp[2];
  if ((res = write(file, &reg_addr, 1)) < 0) {
    perror("write() register address failed");
    return res;
  }

  if ((res = read(file, tmp, 2)) < 0) {
    perror("read() data failed");
    return res;
  }

  *data = (tmp[0] << 8) + tmp[1];
  return 0;
}

int i2c_write_byte(int file, uint8_t reg_addr, uint16_t data) {
  /* must concatenate address and data,                         *
   * otherwise transfer will be terminated before data is sent. */
  int res;
  uint8_t buf[3] = {reg_addr, (data >> 8), (data & 0xff)};

  if ((res = write(file, buf, 3)) < 0) {
    perror("write() register address / data failed");
    return res;
  }

  return 0;
}

/* TMP007-specific functions */

double tmp007_reg_to_mv(int16_t reg) {
  return reg * 156.25f / 1e6;
}

double tmp007_reg_to_temp(int16_t reg) {
  return (reg >> 2) * 0.03125f;
}

int tmp007_print_all(int file) {
  int res;
  int16_t volt;
  int16_t tdie;
  // TODO: config
  int16_t tobj;
  // TODO: status
  int16_t tobjh;
  int16_t tobjl;
  int16_t tdieh;
  int16_t tdiel;
  // TODO: cal
  uint16_t devid;
  // TODO: mem status

  if ((res = i2c_read_word(file, TMP007_REG_VOLT, (uint16_t *)&volt)) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, TMP007_REG_TDIE, (uint16_t *)&tdie)) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, TMP007_REG_TOBJ, (uint16_t *)&tobj)) < 0) {
    return res;
  }

  if ((res = i2c_read_word(file, TMP007_REG_TDIE_H, (uint16_t *)&tdieh)) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, TMP007_REG_TDIE_L, (uint16_t *)&tdiel)) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, TMP007_REG_TOBJ_H, (uint16_t *)&tobjh)) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, TMP007_REG_TOBJ_L, (uint16_t *)&tobjl)) < 0) {
    return res;
  }

  if ((res = i2c_read_word(file, TMP007_REG_DEVID, &devid)) < 0) {
    return res;
  }

  fputs("All temperatures are in degree Celsius.\n", stdout);
  fprintf(stdout, "Device ID: 0x%04x\n", devid);
  fprintf(stdout, "Voltage: %.4lf mV\nLocal Temperature: %.2lf\nRemote Temperature: %.2lf\n", tmp007_reg_to_mv(volt), tmp007_reg_to_temp(tdie), tmp007_reg_to_temp(tobj));
  fprintf(stdout, "Alarm(L/H): Local %.2lf / %.2lf, Remote %.2lf / %.2lf\n", tmp007_reg_to_temp(tdiel), tmp007_reg_to_temp(tdieh), tmp007_reg_to_temp(tobjl), tmp007_reg_to_temp(tobjh));

  return 0;
}

/* CLI */

/******************************************************************************
 * Option list (operations will be carried out in argument list order):
 * a <int> - override address
 * A       - print all
 * b <int> - set bus number (must be done prior to any other operation)
 * l       - local temperature
 * o       - object temperature
 * TODO: F/C switch
 * TODO: L - Oversample + wait local become stable
 *****************************************************************************/

void print_help(const char *self) {
  fprintf(stderr, "\
  Userspace I2C utility for: Texas Instruments TMP007 Remote Temperature Sensor\n\
  (C) Chi Zhang (dword1511) <zhangchi866@gmail.com>\n\
  \n\
  Usage:\n\
    %s -b <bus number> [list of operations]\n\
  \n\
  Operations will be carried out in argument list order.\n\
  Bus number and address can be overrided in the middle of the list.\n\
  \n\
  List of operations:\n\
    -a <int>: override device address (default: 0x%02x, in range 0x03 to 0x7f).\n\
              NOTE: this value will NOT be reset to default after switching\n\
                    bus.\n\
              WARN: use this option only when you know what you are doing!\n\
    -A      : print all information provided by the sensor.\n\
    -b <int>: set bus number (must be set prior to any operations).\n\
              NOTE: you can use `i2cdetect -l' to list I2C buses present in the\n\
                    system.\n\
    -l      : print local (die) temperature.\n\
    -o      : print remote (object) temperature.\n\
  \n\
  Example:\n\
    Print object temperature measured by TMP007 on i2c-1:\n\
      %s -b 1 -o\n\
  \n", self, TMP007_DEVAD_DEF, self);
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
  int ad = TMP007_DEVAD_DEF;
  opterr = 0;
  while ((c = getopt(argc, argv, "a:Ab:lo")) != -1) {
    switch (c) {
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

      case 'A': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        if ((res = tmp007_print_all(file)) < 0) {
          close(file);
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

      case 'l': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        int16_t tdie;
        if ((res = i2c_read_word(file, TMP007_REG_TDIE, (uint16_t *)&tdie)) < 0) {
          close(file);
          return res;
        }
        fprintf(stdout, "Local Temperature: %.2f C\n", tmp007_reg_to_temp(tdie));
        break;
      }

      case 'o': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        int16_t tobj;
        if ((res = i2c_read_word(file, TMP007_REG_TOBJ, (uint16_t *)&tobj)) < 0) {
          close(file);
          return res;
        }
        fprintf(stdout, "Remote Temperature: %.2f C\n", tmp007_reg_to_temp(tobj));
        break;
      }

      case '?': {
        handle_bad_opts();
        print_help(argv[0]);
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
