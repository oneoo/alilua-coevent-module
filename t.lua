L = require('coevent')
local http = require('httpclient')

ff = io.open('t.lua')
L(function()
	print('start')

	print(
		http('http://aaa:bbb@dx.longhui.cn', {
					pool_size = 60,
					data = 'aaa',
					header = 
						'cookie: a=ccc;'
					,
					timeout=30000,
				})
		http('http://aaa:bbb@dx.longhui.cn', {
					pool_size = 60,
					data = 'aaa',
					header = 
						'cookie: a=ccc;'
					,
					timeout=30000,
				})
	)
end
)
print('end')
