17monip for Lua
===

17mon.cn IP地址库的使用扩展

**注意**: 请自行到`http://tool.17mon.cn/ipdb.html`下载最新的IP库文件

编译
===

####基于系统默认的Lua环境
`$ make`

####使用LuaJit并指定路径
`$ make LUAJIT=/usr/local/lib`

`$ make install`

使用方法
===

`require('monip')`

###monip.init('path')

初始化IP库信息（整个文件载入内存）

###monip.find('ip')

根据IP查找对应的地区信息（不支持 Hostname）

返回格式：table，如：{'中国', '浙江', '杭州'}