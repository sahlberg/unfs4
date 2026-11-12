BIN  = unfs4-test
OBJS = unfs4-test.o unfs4.o
CFLAGS += -Os -Wall
FEATURES = -DUNFS4_FEATURE_OPENDIR -DUNFS4_FEATURE_WRITE -DUNFS4_FEATURE_CREATE \
	   -DUNFS4_FEATURE_STAT -DUNFS4_FEATURE_MKDIR -DUNFS4_FEATURE_UNLINK \
	   -DUNFS4_FEATURE_RENAME -DUNFS4_FEATURE_TRUNCATE
CFLAGS += $(FEATURES)

all: $(BIN)

clean:
	rm -f $(OBJS) $(BIN) unfs4-core.o unfs4-full.o unfs4-cat-host

%.o: %.c unfs4.h
	gcc -c $< -o $@ $(CFLAGS)

$(BIN): $(OBJS)
	gcc $(OBJS) -o $(BIN)

# Code size of the client alone: no features, and all of them
size:
	gcc -Os -c unfs4.c -o unfs4-core.o
	gcc -Os $(FEATURES) -c unfs4.c -o unfs4-full.o
	size unfs4-core.o unfs4-full.o

# unfs4 on usmb2's tiny TCP/IP stack on a Linux host, on raw sockets. See
# $(USMB2_DIR)/z80/raw.c for what it needs to run.
USMB2_DIR ?= ../usmb2
HOSTCFILES = host/unfs4-cat-host.c unfs4.c $(USMB2_DIR)/z80/raw.c \
	$(USMB2_DIR)/z80/ip.c $(USMB2_DIR)/z80/tcp.c
unfs4-cat-host: $(HOSTCFILES) unfs4.h
	gcc -Wall -O2 -I. -I$(USMB2_DIR)/z80 -DUNFS4_FEATURE_TINY_TCP \
		-DUNFS4_FEATURE_OPENDIR -DUNFS4_FEATURE_REASSEMBLY -DRS232_TPS=100 \
		-o $@ $(HOSTCFILES)
