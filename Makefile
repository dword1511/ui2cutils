CC     := gcc
CFLAGS := -g -Wall

PROGS = ui2c-ds1307 ui2c-ssd1306

all: $(PROGS)

# Special cases
ui2c-ssd1306: ui2c-ssd1306.o
	gcc -o $@ $^ -lpng

.PHONY: clean

clean:
	rm -f *.o $(PROGS)
