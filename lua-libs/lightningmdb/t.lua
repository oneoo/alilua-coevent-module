local lightningmdb=require "llmdb"

local function pt(t)
  for k,v in pairs(t) do
    print(k,v)
  end
end
local function ps(e)
  print("--- env stat")
  pt(e:stat())
  print("---")
end
local function cursor_pairs(cursor_,key_,op_)
  return coroutine.wrap(
    function()
      local k = key_,v
      repeat
        k,v = cursor_:get(k,op_ or lightningmdb.MDB_NEXT)
        if k then
          coroutine.yield(k,v)
        end
      until not k
    end)
end


print("Lightning MDB version:",lightningmdb.version())
print("Lightning error:",lightningmdb.strerror(0))

print("-- globals --")
pt(lightningmdb)

-- env
local e = lightningmdb.env_create()
print(e)
os.execute("mkdir -p ./temp/foo")
print(e:open("./temp/foo",0,420))
print("fixedmap",lightningmdb.MDB_FIXEDMAP)
print("read only",lightningmdb.MDB_RDONLY)

print("-- stats --")
pt(e:stat())

print("-- info --")
pt(e:info())
print("get_path",e:get_path())


--txn
local t = e:txn_begin(nil,0)
print("txn",t)
t:commit()
t = e:txn_begin(nil,0)
print("txn",t)
t:reset()
t:renew()
--t:abort()
local db = t:dbi_open(nil,0)
print("-- txn stat --")
pt(t:stat(db))

t:abort()



local t = e:txn_begin(nil,0)
local d = t:dbi_open(nil,0)

print("adding values:",count)
local j = 0

for v=1,100 do
  print(string.format("%03x",v))
  local rc = t:put(d,string.format("%03x",v),string.format("%d foo bar",v), lightningmdb.MDB_NOOVERWRITE)
  if not rc then
    j = j + 1
  end
end

print(j,"duplicates skipped")
t:commit()
ps(e)

--[[
for v=1,10000 do
  t = e:txn_begin(nil,0)
  local key = string.format("%03x",v)
  if not t:del(d,key,nil) then
    t:abort()
    print('err')
  else
    t:commit()
    --ps(e)
  end
end
]]

t = e:txn_begin(nil,0)
c = t:cursor_open(d)

k,v = c:get("05a", lightningmdb.MDB_SET_KEY)
print(k,v)
k,v = c:get("05a", lightningmdb.MDB_NEXT)
print(k,v)
k,v = c:get("05a", lightningmdb.MDB_NEXT)
print(k,v)

c:close()
t:abort()

local k
t = e:txn_begin(nil,0)
c = t:cursor_open(d)
--[[
for k,v in cursor_pairs(c) do
  print(k,v)
end]]

c:close()
t:abort()

e:dbi_close(d)
e:close()

