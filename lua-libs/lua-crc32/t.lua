local crc32 = require('crc32')

print(crc32.short('gcc -g -fPIC -c *.c -I/usr/local/include -I/usr/local/include/luajit-2.0 -I/usr/local/include/luajit-2.1 -llua -L/usr/lib;'), '==', '3912618984')
print(crc32.long('gcc -g -fPIC -c *.c -I/usr/local/include -I/usr/local/include/luajit-2.0 -I/usr/local/include/luajit-2.1 -llua -L/usr/lib;'), '==', '3912618984')

local c = crc32:init()
c:update('gcc -g -fPIC -c *.c -I/usr/local/include -I/usr/local/include/luajit-2.0')
c:update(' -I/usr/local/include/luajit-2.1 -llua -L/usr/lib;')
print(c:final(), '==', 3912618984)