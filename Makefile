VERSION = 1.0.3
SETUPDB	= ../setupdb
arch := $(shell sh ../print_arch)
DATADIR ?= .

CFLAGS ?= -g -O2 -Wall
CFLAGS += -I$(SETUPDB)
CFLAGS += $(shell pkgconf --cflags gtk+)
CFLAGS += $(shell pkgconf --cflags libglade)
CFLAGS += -DUNINSTALL_UI -DVERSION=\"$(VERSION)\"
CFLAGS += -DDATADIR=\"$(DATADIR)\"

LFLAGS  = $(SETUPDB)/$(arch)/libsetupdb.a
LFLAGS += -rdynamic
LFLAGS += -Wl,-Bstatic
LFLAGS += -lglade
LFLAGS += -Wl,-Bdynamic
LFLAGS += $(shell pkgconf --libs libxml-2.0)
LFLAGS += $(shell pkgconf --libs gtk+) -lgmodule
LFLAGS += -lz
LFLAGS += $(LDFLAGS)

OBJS = uninstall.o uninstall_ui.o

loki_uninstall: $(OBJS) $(SETUPDB)/$(arch)/libsetupdb.a
	$(CC) -o $@ $^ $(LFLAGS)

all: loki_uninstall

clean:
	rm -f $(OBJS) loki_uninstall

distclean: clean
