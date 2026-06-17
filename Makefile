VERSION = 1.0.4
SETUPDB	= ../setupdb
arch := $(shell sh ../print_arch)
DATADIR ?= .
GTK ?= 1

CFLAGS ?= -g -O2 -Wall
CFLAGS += -I$(SETUPDB)
ifeq ($(GTK),1)
CFLAGS += $(shell pkgconf --cflags gtk+)
CFLAGS += $(shell pkgconf --cflags libglade)
CFLAGS += -DDATADIR=\"$(DATADIR)\"
else
CFLAGS += $(shell pkgconf --cflags gtk4)
endif
CFLAGS += -DUNINSTALL_UI -DVERSION=\"$(VERSION)\"

LFLAGS  = $(SETUPDB)/$(arch)/libsetupdb.a
ifeq ($(GTK),1)
LFLAGS += -rdynamic
LFLAGS += -Wl,-Bstatic
LFLAGS += -lglade
LFLAGS += -Wl,-Bdynamic
LFLAGS += $(shell pkgconf --libs gtk+) -lgmodule
else
LFLAGS += $(shell pkgconf --libs gtk4)
endif
LFLAGS += $(shell pkgconf --libs libxml-2.0)
LFLAGS += -lz
LFLAGS += $(LDFLAGS)

OBJS = uninstall.o
ifeq ($(GTK),1)
OBJS += uninstall_ui.o
else
OBJS += uninstall_ui4.o
endif

loki_uninstall: $(OBJS) $(SETUPDB)/$(arch)/libsetupdb.a
	$(CC) -o $@ $^ $(LFLAGS)

all: loki_uninstall

clean:
	rm -f $(OBJS) loki_uninstall

distclean: clean
