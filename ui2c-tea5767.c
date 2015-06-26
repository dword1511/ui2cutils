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


/* TEA5767 Definations */
/* Global */
#define TEA5767_DEVAD_DEF  (0x60)


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

int i2c_write_freq(int file, uint16_t freq_reg) {
  /* must concatenate address and data,                         *
   * otherwise transfer will be terminated before data is sent. */
  int res;
  uint8_t buf[5] = {(freq_reg >> 8), (freq_reg & 0xff), 0xb0, 0x10, 0x00};

  if ((res = write(file, buf, 5)) < 0) {
    perror("write() register address / data failed");
    return res;
  }

  return 0;
}

/* TEA5767-specific functions */

uint16_t tea5767_mhz_to_regs(float mhz) {
  if ((mhz < 76.0f) || (mhz > 108.0f)) {
    return 0;
  }

  return 4 * (mhz * 1000000 + 225000) / 32768;
}


/* CLI */

/******************************************************************************
 * Option list (operations will be carried out in argument list order):
 *****************************************************************************/

void print_help(const char *self) {
  fprintf(stderr, "\
  Userspace I2C utility for: ST-NXP TEA5767 FM Receiver\n\
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
    -b <int>: set bus number (must be set prior to any operations).\n\
              NOTE: you can use `i2cdetect -l' to list I2C buses present in the\n\
                    system.\n\
    -f <flt>: set frequency in MHz.\n\
  \n\
  Example:\n\
    Tune TEA5767 on i2c-1 to 104.1MHz:\n\
      %s -b 1 -f 104.1\n\
  \n", self, TEA5767_DEVAD_DEF, self);
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
  int ad = TEA5767_DEVAD_DEF;
  opterr = 0;
  while ((c = getopt(argc, argv, "a:b:f:")) != -1) {
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

      case 'f': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        float mhz;
        res = sscanf(optarg, "%f", &mhz);
        if ((1 != res) || (mhz < 76.0f) || (mhz > 108.0f)) {
          fprintf(stderr, "ERROR: invalid frequency: `%s'.\n\n", optarg);
          return -EINVAL;
        }

        if ((res = i2c_write_freq(file, tea5767_mhz_to_regs(mhz))) < 0) {
          close(file);
          return res;
        }
        fprintf(stdout, "Frequency set to: %3.1f MHz\n", mhz);
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
