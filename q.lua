local L = require('coevent')
local httprequest = (require "httpclient").httprequest
open_log('/dev/null,1')
L(function()
    while 1 do
        math.randomseed(os.time())
        local n = math.random(100)
        local i = 1
        local ts = {}
        print('start', n)
        for i=1,n do
            ts[i] = newthread(function()
                    local res,e = httprequest('http://localhost:19827/', {timeout=2000})
                    if e then print(res and res.status or e) end
                end)
        end
        wait(ts)
    end
end)