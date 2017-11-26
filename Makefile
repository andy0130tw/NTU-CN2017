CC      ?= gcc
CFLAGS  = -O2 -std=c99 -DSILENT
CFLAGS  = -O2 -std=c99
TARGETS = sender receiver agent
CHDRS   = udp_shared.h

all: $(TARGETS) $(CHDRS)

run-sender: sender
	AGENT=127.0.0.1:3333  \
	RECV=127.0.0.1:6666   \
	./sender 127.0.0.1:8080 X_train 16

debug: CFLAGS += -g -Og
debug: all

clean:
	rm -f $(TARGETS)

%: %.c $(CHDRS)
	$(CC) $(CFLAGS)    $<   -o $@
