PKG_CONFIG ?= pkg-config
CC ?= cc

CFLAGS += -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -I../batman-adv/include/uapi
LDLIBS += $(shell $(PKG_CONFIG) --libs libnl-3.0 libnl-genl-3.0)
CFLAGS += $(shell $(PKG_CONFIG) --cflags libnl-3.0 libnl-genl-3.0)

all: battpctl

battpctl: battpctl.o

clean:
	rm -f battpctl battpctl.o

.PHONY: all clean
