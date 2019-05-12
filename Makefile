ROOTDIR = ../..
include $(ROOTDIR)/Make.env
OUTFILE = main
OUTLIB = libjsonrpc.so
SRC_C = $(wildcard *.c)
OBJS = $(patsubst %.c,%.o,$(SRC_C))
OBJS := $(filter-out main.o,$(OBJS))
CFLAGS = $(G_CFLAGS) --std=c99 -D_GNU_SOURCE -I../base_lib 
LIBRARY = $(G_LDFLAGS) -L../base_lib -lbase -ljson -lm -lpthread -lssl

# Pattern rules

%.o : %.c
	gcc -fPIC $(CFLAGS) -c -o $@ $<


# Build rules
all: $(OUTFILE) 

$(OUTFILE): $(OUTLIB) main.o
	gcc $(CFLAGS) -o $@ main.o $(OBJS) $(LIBRARY)

main.o : main.c
	gcc $(CFLAGS) -c -o $@ $<

$(OUTLIB): $(OBJS)
	gcc -shared -o $@ $^ $(LIBRARY)

install:
	$(call cy_install,$(OUTLIB),$(PREFIX)/cylanlib/)

# Clean this project
clean:
	rm -f $(OUTFILE)
	rm -f $(OUTLIB)
	rm -f $(OBJS)
	rm -f main.o


