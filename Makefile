CC = gcc
PROG = ghost

CFLAGS  = -std=gnu99 -Wall -O2
CFLAGS += -Werror-implicit-function-declaration
CFLAGS += -Wshadow

OBJS  = ghost.o steg.o

all: $(PROG)

ghost: $(OBJS)
	@echo "  LINK    $@"
	@$(CC) $(LDFLAGS) $^ -o $@

-include $(patsubst %.o,.%.d,$(OBJS))

%.o: %.c
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) -MMD -MF .$*.d -c $<

clean:
	rm -f $(PROG) *.o *.so .*.d

.PHONY: all clean
