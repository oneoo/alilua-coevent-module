alilua coevent module
=========
A Lua epoll base coroutine module (Only support Linux platform)

Install
--------
$tar zxf alilua-coevent-module-*.tar.gz
$cd alilua-coevent-module
$sudo make install clean

Start
--------
###single loop
    local L = require('coevent')
    L(function()
        local cok,err = cosocket.tcp()
        local r,err = cok:connect('www.qq.com', 80)
        if not r then
            print('Connect Error:', err)
        else
            cok:send('GET / HTTP/1.0\r\n')
            cok:send({
                'Host: www.qq.com\r\n',
                'User-Agent: Mozilla/5.0\r\n',
                'Connection: close\r\n\r\n',
                })
            local datas,datas_c,data_len = {}, 1, 0
            while true do
                local r,e = cok:receive('*l')
                if r then
                    datas[datas_c] = r
                    data_len = data_len+#r
                    datas_c = datas_c+1
                else
                    if e then
                        print('Read Error:', e)
                    end
                    break
                end
            end
            
            print('Readed:', data_len)
            cok:close()
        end
    end)

###multi loop
    local L = require('coevent')

    function test_case(test_id)
        local cok,err = cosocket.tcp()
        local r,err = cok:connect('www.qq.com', 80)
        if not r then
            print('Connect Error:', err)
        else
            cok:send('GET / HTTP/1.0\r\n')
            cok:send({
                'Host: www.qq.com\r\n',
                'User-Agent: Mozilla/5.0\r\n',
                'Connection: close\r\n\r\n',
                })
            local datas,datas_c,data_len = {}, 1, 0
            while true do
                local r,e = cok:receive('*l')
                if r then
                    datas[datas_c] = r
                    data_len = data_len+#r
                    datas_c = datas_c+1
                else
                    if e then
                        print('Read Error:', e)
                    end
                    break
                end
            end
            
            print('#'..test_id..' Readed:', data_len)
            cok:close()
            
            return data_len
        end
    end
    
    L(function()
        local t1 = newthread(test_case, 1)
        local t2 = newthread(test_case, 2)
        
        print(coroutine_wait(t1)) --print readed len
        print(coroutine_wait(t2)) --print readed len
    end)

Epoll lopp Directives
---------
**synctx:** coevent(function)

    local L = require('coevent')
    L(function()
        -- async IO codes
    end)
 
cosocket Directives
---------
###cosocket.tcp()
Creates and returns a TCP or stream-oriented unix domain socket object, The following methods are supported on this object:

* connect

* send

* receive

* close

* settimeout

###cok:connect
**syntax:** ok, err = cok:connect(host, port)

Attempts to connect a TCP socket object to a remote server

###cok:send
**syntax:** ok, err = cok:send(data)

Sends data without blocking on the current TCP connection.

The input argument data can either be a Lua string or a (nested) Lua table holding string fragments. 

###cok:receive
**syntax:** data, err = cok:receive(pattern?)

The input argument data can either be '*l', '*a' or a number.

* ***l**: reads a line of text from the socket. The line is terminated by a Line Feed (LF) character (ASCII 10), optionally preceded by a Carriage Return (CR) character (ASCII 13). The CR and LF characters are not included in the returned line. In fact, all CR characters are ignored by the pattern.

* ***a**: reads from the socket until the connection is closed. No end-of-line translation is performed;

**If no argument is specified, then it is assumed to be the pattern '*a'**

###cok:colse
**syntax:** ok, err = cok:close()

Closes the current TCP or stream unix domain socket. It returns the 1 in case of success and returns nil with a string describing the error otherwise.

Socket objects that have not invoked this method (and associated connections) will be closed when the socket object is released by the Lua GC (Garbage Collector) or the current client HTTP request finishes processing.

###cok:settimeout
**syntax:** cok:settimeout(sec)

Set the timeout value in milliseconds for subsequent socket operations (connect, receive, and iterators returned from receiveuntil).

thread Directives
--------
###newthread

**synctx:** t = newthread(function() end, ...) 

Creates a user Lua coroutines with a Lua function, and returns a coroutine object.

Similar to the standard Lua coroutine.create API, but works in the context of the Lua coroutines created by alilua.

###coroutine_wait
**synctx:** returns = coroutine_wait(t)

Waits on one child "light threads" and returns the results (either successfully or with an error).

###swop
**synctx:** swop()

Sleeps for the little times without blocking.

    for i=1,10000 do
        swop() --auto sleep a little times
        print(i)
    end

