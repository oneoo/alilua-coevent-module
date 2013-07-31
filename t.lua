local L = require('coevent')
print('start')
local mysql = require('mysql')

local f = function()
    local db, err = mysql:new()
    if not db then
        print("failed to instantiate mysql: ", err)
        return
    end
    
    local ok, err, errno, sqlstate = db:connect{
        host = "127.0.0.1",
        port = 3306,
        pool_size = 20,
        database = "d1",
        user = "u1",
        password = "u11111",
        max_packet_size = 1024 * 1024 }

    if not ok then
        print("failed to connect: ", err, ": ", errno, " ", sqlstate)
        return
    end
    
    local db2, err = mysql:new()
    if not db2 then
        print("failed to instantiate mysql: ", err)
        return
    end
    
    local ok, err, errno, sqlstate = db2:connect{
        host = "127.0.0.1",
        port = 3306,
        pool_size = 20,
        database = "d1",
        user = "u1",
        password = "u11111",
        max_packet_size = 1024 * 1024 }

    if not ok then
        print("failed to connect: ", err, ": ", errno, " ", sqlstate)
        return
    end

    print("connected to mysql.")

    res, err, errno, sqlstate =
	db:query("select * from t1")
    if not res then
        print("bad result: ", err, ": ", errno, ": ", sqlstate, ".")
        return
    else
        print("queryed")
    end
    
    db:close()

    db2:close()
end

L(f)
print('2')
L(f)


print('end')
