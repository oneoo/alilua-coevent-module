MODNAME= coevent
CC = gcc
OPTIMIZATION = -O3
CFLAGS = -lssl -lcrypto -lm -lpthread -lz

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
	$(CC) objs/*.o -o $(MODNAME).so -shared -fPIC $(CFLAGS) $(LIBLUA)

$(MODNAME).o:
	[ -f merry/merry.h ] || (git submodule init && git submodule update)
	[ -d objs ] || mkdir objs;
	cd objs && $(CC) -g -fPIC -c ../merry/common/*.c;
	cd objs && $(CC) -g -fPIC -c ../merry/se/*.c;
	cd objs && $(CC) -g -fPIC -c ../merry/*.c;
	cd objs && $(CC) -g -fPIC -c ../src/*.c;

	[ -f bit.so ] || (cd lua-libs/LuaBitOp-1.0.2 && make LIBLUA="$(LIBLUA)" && cp bit.so ../../ && make clean);
	[ -f cjson.so ] || (cd lua-libs/lua-cjson-2.1.0 && make LIBLUA="$(LIBLUA)" && cp cjson.so ../../ && make clean);
	[ -f zlib.so ] || (cd lua-libs/lzlib && make LIBLUA="$(LIBLUA)" && cp zlib.so ../../ && make clean && rm -rf *.o);

install:
	`cd objs` && $(CC) -O3 objs/*.o -o objs/$(MODNAME).so -shared $(CFLAGS) $(LIBLUA);
	cp objs/*.so `lua installpath.lua .so`;
	cp mysql.lua `lua installpath.lua .lua`;
	cp redis.lua `lua installpath.lua .lua`;
	cp memcached.lua `lua installpath.lua .lua`;
	cp httpclient.lua `lua installpath.lua .lua`;
	rm objs/*.so;

clean:
	rm *.so;
	rm -r objs;