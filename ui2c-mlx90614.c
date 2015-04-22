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


/* MLX90614 Definations */
/* Global */
#define MLX90614_DEVAD   (0x5a)

/* RAM, Read-only */
#define MLX90614_RAWIR1  (0x04)
#define MLX90614_RAWIR2  (0x05)
#define MLX90614_TA      (0x06)
#define MLX90614_TOBJ1   (0x07)
#define MLX90614_TOBJ2   (0x08)

/* EEPROM, Write with care  */
#define MLX90614_TOMAX   (0x20)
#define MLX90614_TOMIN   (0x21)
#define MLX90614_PWMCTRL (0x22)
#define MLX90614_TARANGE (0x23)
#define MLX90614_EMSSVTY (0x24)
#define MLX90614_CONFIG1 (0x25) /* NOTE: Altering bit 3 will cancel factory calibration. */
#define MLX90614_ADDRESS (0x2e)
#define MLX90614_UNKNOWN1 (0x2f) /* Mentioned writtable but no documentation */
#define MLX90614_UNKNOWN2 (0x39) /* Mentioned writtable but no documentation */
#define MLX90614_ID1     (0x3c)
#define MLX90614_ID2     (0x3d)
#define MLX90614_ID3     (0x3e)
#define MLX90614_ID4     (0x3f)

/* Additional */
#define MLX90614_FLAG    (0xf0)
#define MLX90614_SLEEP   (0xff)


/* TODO: turn into functions */
#define MLX90614_PWM_SGL (1 << 0)
#define MLX90614_PWM_EXT (0 << 0)
#define MLX90614_PWM_EN  (1 << 1)
#define MLX90614_PWM_PP  (1 << 2)
#define MLX90614_PWM_RELAY  (1 << 3)


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
  int res;

  if (NULL == data) {
    return -EFAULT;
  }

  if ((res = i2c_smbus_read_word_data(file, reg_addr)) < 0) {
    /* Try again */
    if ((res = i2c_smbus_read_word_data(file, reg_addr)) < 0) {
      perror("i2c_smbus_read_word_data");
      return res;
    }
  }

  *data = res;
  return 0;
}

/* MLX90614-specific functions */
double mlx90614_reg_to_temp(uint16_t reg) {
  /* NOTE: register range is 0x27ad 0x7fff, temp range is -70.01 C to +382.19 C */
  return reg * 0.02f - 273.15f;
}

int mlx90614_print_all(int file) {
  int res;
  uint16_t id[4] = {0x0000, 0x0000, 0x0000, 0x0000};
  uint16_t ta;
  uint16_t tobj1;
  uint16_t tobj2;

  if ((res = i2c_read_word(file, MLX90614_ID1, &id[0])) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, MLX90614_ID2, &id[1])) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, MLX90614_ID3, &id[2])) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, MLX90614_ID4, &id[3])) < 0) {
    return res;
  }

  if ((res = i2c_read_word(file, MLX90614_TA, &ta)) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, MLX90614_TOBJ1, &tobj1)) < 0) {
    return res;
  }
  if ((res = i2c_read_word(file, MLX90614_TOBJ2, &tobj2)) < 0) {
    return res;
  }

  fputs("All temperatures are in degree Celsius.\n", stdout);
  fprintf(stdout, "Device ID: %04x%04x%04x%04x\n", id[0], id[1], id[2], id[3]);
  fprintf(stdout, "Local Temperature: %.2lf\nRemote Temperature 1: %.2lf\nRemote Temperature 2: %.2lf\n",
          mlx90614_reg_to_temp(ta), mlx90614_reg_to_temp(tobj1), mlx90614_reg_to_temp(tobj2));

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
 *****************************************************************************/

void print_help(const char *self) {
  fprintf(stderr, "\
  Userspace I2C utility for: Melexis MLX90614 Remote Temperature Sensor\n\
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
    Print object temperature measured by MLX90614 on i2c-1:\n\
      %s -b 1 -o\n\
  \n", self, MLX90614_DEVAD, self);
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
  int ad = MLX90614_DEVAD;
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

        if ((res = mlx90614_print_all(file)) < 0) {
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

        uint16_t ta;
        if ((res = i2c_read_word(file, MLX90614_TA, &ta)) < 0) {
          close(file);
          return res;
        }
        fprintf(stdout, "Local Temperature: %.2lf C\n", mlx90614_reg_to_temp(ta));
        break;
      }

      case 'o': {
        if (file < 0) {
          fprintf(stderr, "ERROR: bus number not set prior to operation.\n\n");
          print_help(argv[0]);
          return -EINVAL;
        }

        uint16_t tobj1, tobj2;
        if ((res = i2c_read_word(file, MLX90614_TOBJ1, &tobj1)) < 0) {
          close(file);
          return res;
        }
        if ((res = i2c_read_word(file, MLX90614_TOBJ2, &tobj2)) < 0) {
          close(file);
          return res;
        }
        fprintf(stdout, "Remote Temperature 1: %.2lf C\nRemote Temperature 2: %.2lf C\n", mlx90614_reg_to_temp(tobj1), mlx90614_reg_to_temp(tobj2));
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
