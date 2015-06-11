cfgclient_test:cfgclient_test.o
	gcc -g -Wall cfgclient_test.o -o cfgclient_test -L/data/refactor/mycode/3rd/lib/ -L../lib -lcfgclient -lpthread -lm
cfgclient_test.o:cfgclient_test.c
	gcc -g -Wall -c cfgclient_test.c -I../include

clean:
	rm -rf *.o cfgclient_test
