MODNAME= monip
CC = gcc
OPTIMIZATION = -O3
CFLAGS = -lm -lpthread

INCLUDES=-I/usr/local/include -I/usr/local/include/luajit-2.0 -I/usr/local/include/luajit-2.1

ifeq ($(LUAJIT),)
ifeq ($(LUA),)
LIBLUA = -llua -L/usr/lib
else
LIBLUA = -L$(LUA) -llua 
INCLUDES = -I$(LUAJIT)/../include/
endif
else
LIBLUA = -L$(LUAJIT) -lluajit-5.1
INCLUDES = -I$(LUAJIT)/../include/luajit-2.0 -I$(LUAJIT)/../include/luajit-2.1
endif

all:$(MODNAME).o
	$(CC) -o $(MODNAME).so $(OPTIMIZATION) -shared -fPIC $(CFLAGS) $(LIBLUA) *.o

$(MODNAME).o:
	$(CC) -g -fPIC -c *.c $(INCLUDES) $(LIBLUA);

install:
	`cd objs` && $(CC) $(OPTIMIZATION) *.o -o $(MODNAME).so -shared $(CFLAGS) $(LIBLUA);
	cp *.so `lua installpath.lua .so`;

clean:
	rm *.o;
	rm *.so;