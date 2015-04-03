CC     := gcc
CFLAGS := -g -Wall

PROGS = ui2c-ds1307

all: $(PROGS)


.PHONY: clean

clean:
	rm -f *.o $(PROGS)
