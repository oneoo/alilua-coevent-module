MODNAME= coevent
CC = gcc
OPTIMIZATION = -O3
CFLAGS = -lssl -lcrypto

all:$(MODNAME).o
	$(CC) -shared objs/*.o -o $(MODNAME).so $(CFLAGS)

$(MODNAME).o:
	[ -d objs ] || mkdir objs;
	cd objs && $(CC) -g -fPIC -c ../se/*.c;
	cd objs && $(CC) -g -fPIC -c ../src/*.c;
	`cd ../` && $(CC) -g -shared objs/*.o -o $(MODNAME).so $(CFLAGS)

install:
	cd objs && $(CC) -O3 -fPIC -c ../se/*.c;
	cd objs && $(CC) -O3 -fPIC -c ../src/*.c;
	`cd ../` && $(CC) -shared -O3 objs/*.o -o $(MODNAME).so $(CFLAGS)
	install $(MODNAME).so $< `lua installpath.lua $(MODNAME)`

clean:
	rm -r objs;
	rm $(MODNAME).so;