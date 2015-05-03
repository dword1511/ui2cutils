CC     ?= gcc
CFLAGS ?= -g -Wall

PROGS = ui2c-ds1307 ui2c-ssd1306 ui2c-tmp007 ui2c-mlx90614

###############################################################################

OBJS  = $(PROGS:%=%.o)

all: $(PROGS)

.SUFFIXES:
.SECONDARY: $(OBJS)

%.o: %.c
	@echo "  CC    " $@
	@$(CC) $(CFLAGS) -c -o $@ $<

%: %.o
	@echo "  LD    " $@
	@$(CC) $^ $(LDFLAGS) -o $@

# Special cases
ui2c-ssd1306: ui2c-ssd1306.o
	@echo "  LD    " $@
	@$(CC) $^ $(LDFLAGS) -lpng -o $@

# Documentation
README.html: README.md
	@echo "  MD    " $@
	@markdown $< > $@

.PHONY: clean

clean:
	@echo " CLEAN  " "."
	@rm -f *.o $(PROGS)
