MODNAME= llmdb
CC = gcc
OPTIMIZATION = -O3
CFLAGS = -lm

ifeq ($(LUAJIT),)
ifeq ($(LUA),)
LIBLUA = -llua -L/usr/lib
else
LIBLUA = -L$(LUA) -llua
endif
else
LIBLUA = -L$(LUAJIT) -lluajit-5.1
endif

INCLUDES=-I/usr/local/include -I/usr/local/include/luajit-2.0 -I/usr/local/include/luajit-2.1

all:$(MODNAME).o
	$(CC) *.o -o $(MODNAME).so $(OPTIMIZATION) -shared -fPIC $(CFLAGS) $(LIBLUA)

$(MODNAME).o:
	$(CC) -g -fPIC -c *.c $(INCLUDES);

install:
	`cd objs` && $(CC) $(OPTIMIZATION) *.o -o $(MODNAME).so -shared $(CFLAGS) $(LIBLUA);
	cp *.so `lua installpath.lua .so`;

clean:
	rm *.so;
	rm -r *.o;