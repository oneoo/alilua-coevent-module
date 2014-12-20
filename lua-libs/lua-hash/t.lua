require('hash')

local s = 'd'

print(djb2_hash(s))
print(fnv1a_32(s))
print(fnv1a_64(s))
print(murmurhash(s))
print(crapwow_hash(s))
print(lua_hash(s))