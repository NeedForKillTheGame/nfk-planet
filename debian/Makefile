MAKE = make
CC = gcc
CFLAGS = -fpic -Wall -O3 -pthread -D_GNU_SOURCE
INCDIR = /usr/local/include
LIBDIR = /usr/local/lib
SHELL = /bin/bash
STRIP = strip
SRCDIR = ../src
BINDIR = ../bin

all:
	$(CC) $(CFLAGS) $(SRCDIR)/nfkplanet.c -o $(BINDIR)/nfkplanet -I$(INCDIR) -L$(LIBDIR) 

debug:
	$(CC) $(CFLAGS) -g $(SRCDIR)/nfkplanet.c -o $(BINDIR)/nfkplanetd -I$(INCDIR) -L$(LIBDIR) -D_DEBUG

    