MODNAME= coevent
CC = gcc
OPTIMIZATION = -O3
CFLAGS = -lssl -lcrypto -lm -lpthread

ifeq ($(LUAJIT),)
ifeq ($(LUA),)
LIBLUA = -llua -L/usr/lib
else
LIBLUA = -L$(LUA) -llua
endif
else
LIBLUA = -L$(LUAJIT) -lluajit-5.1
endif

all:$(MODNAME).o
	$(CC) -shared objs/*.o -o $(MODNAME).so -shared $(CFLAGS) $(LIBLUA)

$(MODNAME).o:
	[ -d objs ] || mkdir objs;
	cd objs && $(CC) -g -fPIC -c ../merry/common/*.c;
	cd objs && $(CC) -g -fPIC -c ../merry/se/*.c;
	cd objs && $(CC) -g -fPIC -c ../merry/*.c;
	cd objs && $(CC) -g -fPIC -c ../src/*.c;
	`cd ../` && $(CC) -g objs/*.o -o $(MODNAME).so -shared $(CFLAGS) $(LIBLUA)

install:
	cd objs && $(CC) -O3 -fPIC -c ../se/*.c;
	cd objs && $(CC) -O3 -fPIC -c ../src/*.c;
	`cd ../` && $(CC) -O3 objs/*.o -o $(MODNAME).so $(CFLAGS) $(LIBLUA)
	install $(MODNAME).so $< `lua installpath.lua $(MODNAME)`

clean:
	rm -r objs;
	rm $(MODNAME).so;