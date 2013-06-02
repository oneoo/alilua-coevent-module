local L = require('coevent')
print('start')


local mysql = require "mysql"
local cjson = require "cjson"
local db = mysql:new()

local memcached = require "memcached"
local memc = memcached:new()

function test_mysql()
	local db_ok, err, errno, sqlstate = db:connect({
					host = "localhost",
					port = 3306,
					database = "yo2.com",
					user = "root",
					password = "uuu123"})
	print('start test_mysql')
	local st = longtime()


	if not db_ok then
		print("failed to connect: ", err, ": ", errno, " ", sqlstate)
		return
	end
	print('---------------------------------------mysql connected')
	
	--local t = newthread(test_http_client, 200, 'www.163.com', '/')
	local tt = newthread(test_http_client, 300, 'www.163.com', '/rss/')
print('used:'..((longtime()-st)/1000))
	for n=1,1 do
		local bytes, err = db:send_query("select * from users;")
		if not bytes then
			print("failed to send query: ", err)
		end
		
		print('used:'..((longtime()-st)/1000))
		local res, err, errno, sqlstate = db:read_result()
		if not res then
			print("bad result: ", err, ": ", errno, ": ", sqlstate, ".")
		end
		print('used:'..((longtime()-st)/1000))
		print(n, "result: ", cjson.encode(res))
	end
	
	
	
	print('test_mysql be end  used:'..((longtime()-st)/1000));
	coroutine_wait(t)
	coroutine_wait(tt)
	
	db:close()

	print('test_mysql ended')
end

function test_memcached()
	local ok, err = memc:connect("localhost", 11211)
	if not ok then
		print("failed to connect: ", err)
		return
	end

	local ok, err = memc:flush_all()
	if not ok then
		print("failed to flush all: ", err)
		--return
	end

	print("flush: ", ok);
	
	local ok, err = memc:set("dog", 32)
	if not ok then
		print("failed to set dog: ", '['..err..']')
		return
	end

	local t = longtime()
	for i = 1, 20 do
		swop()
		local res, flags, err = memc:get("dog")
		if err then
			print("failed to get dog: ", '['..err..']')
			return
		end

		if not res then
			print("dog not found")
			return
		end

		print("dog: ", res, " (flags: ", flags, ")")
	end
	print('times:', (longtime()-t)/1000)
end

local redis = require "redis"
local red = redis:new()
function test_redis()

	red:set_timeout(1000) -- 1 sec

	local ok, err = red:connect("localhost", 6379)
	if not ok then
		print("failed to connect: ", err)
		return
	end

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

	--red:close()
end

function test_http_client(id, host, uri)
	--print('start test_http_client', id, host, uri)
	local cok = cosocket.tcp()
	local r,e = cok:connect(host, 80)

	if not r then print(1, e) return false end
	--print('----------------------------------------connected!!!', id)
	
	if not uri then
		cok:send('GET / HTTP/1.1\r\nHost: '..host..'\r\nUser-Agent: Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6)\r\n')
	else
		cok:send('GET '..uri..' HTTP/1.1\r\nHost: '..host..'\r\nUser-Agent: Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6)\r\n')
	end
	if not cok:send('Connection: close\r\n\r\n') then print('send error') return false end
	
	local s,e,oss,oss2,oss3,oss4,kc
	kc = 0
	while 1 do
		swop()
		oss4 = oss3
		oss3 = oss2
		oss2 = oss
		oss = s
		s,e = cok:receive('*l')
		if s then kc = kc + #s end
		--print(id, s)
		--print(id,'read ', s and #s or -1)
		if not s then 
			--print(id, e, s, kc)
		break end
	end

	if oss4 then
		oss = oss4..oss3..oss2 .. oss
		oss = oss:sub(#oss-40, #oss)
	else
		oss = ''
	end
	cok:close()
	cok = nil
--print(oss)
	--print('test_http_client ended', id, (oss:find('</html>') or oss:find('2006')) and true or false, oss)
	if not e and not s then os.exit(1) end
	return id, (oss:find('</html>') or oss:find('2006')) and true or false, kc
end
--collectgarbage('stop')

local af = function()
	coroutine_wait(newthread(test_mysql))
	coroutine_wait(newthread(test_memcached))
	coroutine_wait(newthread(test_redis))
	
	--coroutine_wait(newthread(test_http_client, 0, 'wiki.upyun.com', '/index.php?title=%E9%A6%96%E9%A1%B5'))
	local t = longtime()
	--test_http_client(1, 'www.163.com') test_http_client(2, 'www.163.com') test_http_client(3, 'www.163.com')
	
	--t1 = newthread(test_http_client, 1, 'www.163.com')
	--t2 = newthread(test_http_client, 2, 'weibo.com')
	--t3 = newthread(test_http_client, 3, 'www.163.com')
	--coroutine_wait(newthread(test_http_client, 1, 'wiki.upyun.com', '/index.php?title=%E9%A6%96%E9%A1%B5'))
	local ts = {}
	for i=1,100 do swop()
		table.insert(ts, newthread(test_http_client, i+100, 'wiki.upyun.com', '/index.php?title=%E9%A6%96%E9%A1%B5'))
		--table.insert(ts, newthread(test_http_client, i+200, 'www.qq.com', '/'))
		--table.insert(ts, newthread(test_http_client, i+300, 'news.qq.com', '/'))
	end
	
	coroutine_wait(t1)
	coroutine_wait(t2)
	coroutine_wait(t3)
	
	for i=1,1000 do
		if not ts[i] then break end
		print(i+100, coroutine_wait(ts[i]))
	end
	
	print('times:', (longtime()-t)/1000)

	
	
	local t = longtime()
	local ts = {}
	for i=1,10 do --swop()
		print(i, coroutine_wait(newthread(test_http_client, i+100, 'wiki.upyun.com', '/index.php?title=%E9%A6%96%E9%A1%B5')))
	end
	print('times:', (longtime()-t)/1000)
	
	ts = nil
	
	
end

for u = 1,1 do
	L(af)
end

L(function()
	coroutine_wait(newthread(test_mysql))
end)
print('end')