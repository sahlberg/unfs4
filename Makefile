BIN  = unfs4-test
OBJS = unfs4-test.o unfs4.o
CFLAGS += -Os -Wall
FEATURES = -DUNFS4_FEATURE_OPENDIR -DUNFS4_FEATURE_WRITE -DUNFS4_FEATURE_CREATE \
	   -DUNFS4_FEATURE_STAT -DUNFS4_FEATURE_MKDIR -DUNFS4_FEATURE_UNLINK \
	   -DUNFS4_FEATURE_RENAME
CFLAGS += $(FEATURES)

all: $(BIN)

clean:
	rm -f $(OBJS) $(BIN) unfs4-core.o unfs4-full.o

%.o: %.c unfs4.h
	gcc -c $< -o $@ $(CFLAGS)

$(BIN): $(OBJS)
	gcc $(OBJS) -o $(BIN)

# Code size of the client alone: no features, and all of them
size:
	gcc -Os -c unfs4.c -o unfs4-core.o
	gcc -Os $(FEATURES) -c unfs4.c -o unfs4-full.o
	size unfs4-core.o unfs4-full.o
