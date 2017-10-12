MODNAME= coevent
CC = gcc -g -ggdb
CFLAGS = -lssl -lcrypto -lm -lpthread -lz

INCLUDES=-I/usr/local/include -I/usr/local/include/luajit-2.0 -I/usr/local/include/luajit-2.1

ifeq ($(LUAJIT),)
	ifeq ($(LUA),)
	LIBLUA = -llua -L/usr/lib
	LUABIN = /usr/local/bin/lua
	else
	LIBLUA = -L$(LUA) -llua -Wl,-rpath,$(LUA) -I$(LUA)/../include
	endif

	ifneq ("$(wildcard $(LUA)/lua)","")
	LUABIN = $(LUA)/lua
	endif
	ifneq ("$(wildcard $(LUA)/../bin/lua)","")
	LUABIN = $(LUA)/../bin/lua
	endif
else
	ifneq (,$(wildcard $(LUAJIT)/libluajit.a))
	LIBLUA = $(LUAJIT)/libluajit.a -I$(LUAJIT)
	INCLUDES=-I$(LUAJIT)
	else ifneq (,$(wildcard $(LUAJIT)/src/libluajit.a))
	LIBLUA = $(LUAJIT)/src/libluajit.a -I$(LUAJIT)/src
	INCLUDES=-I$(LUAJIT)/src
	else ifneq (,$(wildcard $(LUAJIT)/lib/libluajit-5.1.a))
	LIBLUA = $(LUAJIT)/lib/libluajit-5.1.a -I$(LUAJIT)/include/luajit-2.0
	INCLUDES=-I$(LUAJIT)/include/luajit-2.0
	else
	LIBLUA = -L$(LUAJIT) -lluajit-5.1 -Wl,-rpath,$(LUAJIT) -I$(LUAJIT)/../include/luajit-2.0 -I$(LUAJIT)/../include/luajit-2.1 -I$(LUAJIT)
	endif

	ifneq ("$(wildcard $(LUAJIT)/luajit)","")
	LUABIN = $(LUAJIT)/luajit
	endif
	ifneq ("$(wildcard $(LUAJIT)/../bin/luajit)","")
	LUABIN = $(LUAJIT)/../bin/luajit
	endif
	ifneq ("$(wildcard $(LUAJIT)/luajit-2.1.0-alpha)","")
	LUABIN = $(LUAJIT)/luajit-2.1.0-alpha
	endif
	ifneq ("$(wildcard $(LUAJIT)/../bin/luajit-2.1.0-alpha)","")
	LUABIN = $(LUAJIT)/../bin/luajit-2.1.0-alpha
	endif

endif

all:$(MODNAME).o
	$(CC) -o $(MODNAME).so -shared -fPIC $(LIBLUA) objs/*.o $(CFLAGS)

$(MODNAME).o:
	[ -f merry/merry.h ] || (git submodule init && git submodule update)
	[ -d objs ] || mkdir objs;
	cd objs && $(CC) -fPIC -c ../merry/common/*.c;
	cd objs && $(CC) -fPIC -c ../merry/se/*.c;
	cd objs && $(CC) -fPIC -c ../merry/se/libeio/*.c;
	cd objs && $(CC) -fPIC -c ../merry/*.c;
	cd objs && $(CC) -fPIC -c ../src/*.c $(INCLUDES);

	[ -f bit.so ] || (cd lua-libs/LuaBitOp-1.0.2 && make LIBLUA="$(LIBLUA)" && cp bit.so ../../ && make clean);
	[ -f cjson.so ] || (cd lua-libs/lua-cjson-2.1.0 && make LIBLUA="$(LIBLUA)" && cp cjson.so ../../ && make clean);
	[ -f zlib.so ] || (cd lua-libs/lzlib && make LIBLUA="$(LIBLUA)" && cp zlib.so ../../ && make clean && rm -rf *.o);
	[ -f llmdb.so ] || (cd lua-libs/lightningmdb && make LIBLUA="$(LIBLUA)" && cp llmdb.so ../../ && make clean && rm -rf *.o);
	[ -f cmsgpack.so ] || (cd lua-libs/lua-cmsgpack && make LIBLUA="$(LIBLUA)" && cp cmsgpack.so ../../ && make clean && rm -rf *.o);
	[ -f monip.so ] || (cd lua-libs/lua-monip && make LIBLUA="$(LIBLUA)" && cp monip.so ../../ && make clean && rm -rf *.o);
	[ -f crc32.so ] || (cd lua-libs/lua-crc32 && make LIBLUA="$(LIBLUA)" && cp crc32.so ../../ && make clean && rm -rf *.o);

install:
	cp objs/*.so `$(LUABIN) installpath.lua .so`;
	cp *.so `$(LUABIN) installpath.lua .so`;
	cp mysql.lua `$(LUABIN) installpath.lua .lua`;
	cp redis.lua `$(LUABIN) installpath.lua .lua`;
	cp memcached.lua `$(LUABIN) installpath.lua .lua`;
	cp httpclient.lua `$(LUABIN) installpath.lua .lua`;
	cp llmdb-client.lua `$(LUABIN) installpath.lua .lua`;
	rm objs/*.so;

clean:
	-rm *.so;
	-rm -r objs;