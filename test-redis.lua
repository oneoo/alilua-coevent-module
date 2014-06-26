local L = require('coevent')
local redis = require "redis"

function test_redis()
    local red = redis:new()
    red:set_timeout(1000) -- 1 sec

    local ok, err = red:connect("localhost", 6379)
    if not ok then
        print("failed to connect: ", err)
        return
    end
    print('---------------------------------------redis connected')

    local res, err = red:hmset("animals", "dog", "bark", "cat", "meow")
    if not res then
        print("failed to set animals: ", err)
        return
    end
    print("hmset animals: ", res)

    local res, err = red:hmget("animals", "dog", "cat")
    if not res then
        print("failed to get animals: ", err)
        return
    end
    for k,v in pairs(res) do print(k, v) end
    print("hmget animals: ", res)

    red:set_keepalive(2)
end

L(function()
    for i=1,3 do
    test_redis()
    end
end)