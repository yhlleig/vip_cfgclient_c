include $(CODE_ROOT)/makefile.comm

MYLIB=-lzookeeper_mt -lpthread

ZKROOT=$(3rd)/zookeeper/zookeeper-3.4.6/src/c
MYINC=-DTHREADED -I../include -I$(ZKROOT)/include -I$(ZKROOT)/generated 

SRC=$(wildcard *.c)

OUTPUT=cfgclient.a

.PHONY: all clean install

all:$(OUTPUT)
	cp libcfgclient.a ../lib/

cfgclient.a:cfgclient.o cfg_keynode.o cfg_util.o 

clean:
	rm -f *.o *.a *.so
