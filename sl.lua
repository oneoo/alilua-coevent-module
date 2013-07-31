local L = require('coevent')
local httpclient = require('httpclient')
print('start')
for i=1,1000 do
os.execute('sleep 1')
print(L(function()

	local t,e = httpclient('https://www.upyun.com', {
				pool_size = 20,
			})
    print('readed:', #t, 'Bytes', #t)
    local t,e = httpclient('http://www.upyun.com', {
				pool_size = 20,
			})
    if not t then print(e) end
    print('readed:', #t, 'Bytes', #t)

	
end
))
end
print('end')
