//gcc -g -o cotest cotest.c cotest-util.c -llua -ldl -lm -lssl -lcrypto && ./cotest
/*
gcc -fPIC -c comodule.c
gcc -shared coevent.o coevent-util.o -o coevent.so -lssl -lcrypto
*/
#include "coevent.h"
int epoll_fd = 0;
lua_State *LM = NULL;
int clearthreads_handler = 0;

static int lua_co_connect(lua_State *L){
	{
		if(!lua_isuserdata(L, 1) || !lua_isstring(L, 2) || !lua_isnumber(L, 3)){
			lua_pushnil(L);
			lua_pushstring(L, "Error params!");
			return 2;
		}
		cosocket_t *cok = (cosocket_t *)lua_touserdata(L, 1);
		if(cok->status > 0){
			lua_pushnil(L);
			lua_pushstring(L, "Aleady connected!");
			return 2;
		}

//printf(" 0x%x connect to %s\n", L, lua_tostring(L, 2));

		const char *host = lua_tostring(L, 2);
		int port = lua_tonumber(L, 3);
		cok->status = 1;
		cok->L = L;
		cok->read_buf = NULL;
		cok->last_buf = NULL;
		cok->total_buf_len = 0;
		cok->buf_read_len = 0;
		int connect_ret = 0;
		cok->fd = tcp_connect(host, port, cok, epoll_fd, &connect_ret);
		
		if(cok->fd == -1){
			lua_pushnil(L);
			lua_pushstring(L, "Init socket error!");
			return 2;
		}else if(cok->fd == -2){
			lua_pushnil(L);
			lua_pushstring(L, "Init socket s_addr error!");
			return 2;
		}else if(cok->fd == -3){
			lua_pushnil(L);
			lua_pushstring(L, "names lookup error!");
			return 2;
		}
		
		if(connect_ret == 0){
			cok->status = 2;
			
			struct epoll_event ev;
			ev.data.ptr = cok;
			ev.events = EPOLLPRI;
			if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cok->fd, &ev) == -1)
				printf("EPOLL_CTL_MOD error: %d %s", __LINE__, strerror(errno));
		
			del_in_timeout_link(cok);
			
			lua_pushboolean(L, 1);
			return 1;
			// is done
		}else if(connect_ret == -1 && errno != EINPROGRESS){
			// is error
			lua_pushnil(L);
			lua_pushstring(L, "Init socket error!");
			return 2;
		}
	}

	return lua_yield(L, 0);
}

static int lua_co_send(lua_State *L){
	{
		if(!lua_isuserdata(L, 1) || (!lua_isstring(L, 2) && !lua_istable(L, 2))){
			lua_pushboolean(L, 0);
			lua_pushstring(L, "Error params!");
			return 2;
		}
		
		cosocket_t *cok = (cosocket_t *)lua_touserdata(L, 1);
		if(cok->status != 2 || cok->fd == -1){
			lua_pushboolean(L, 0);
			lua_pushstring(L, "Not connected!");
			return 2;
		}
		cok->L = L;

		cok->send_buf_ed = 0;
		if(lua_istable(L, 2)){
			cok->send_buf_len = lua_calc_strlen_in_table(L, 2, 2, 1 /* strict */);
			if(cok->send_buf_len > 0){
				if(cok->send_buf_len <= sizeof(cok->_send_buf)){
					cok->send_buf_need_free = NULL;
					lua_copy_str_in_table(L, 2, cok->_send_buf);
					cok->send_buf = cok->_send_buf;
				}else{
					cok->send_buf_need_free = large_malloc(cok->send_buf_len);
					lua_copy_str_in_table(L, 2, cok->send_buf_need_free);
					cok->send_buf = cok->send_buf_need_free;
				}
			}
		}else{
			cok->send_buf = lua_tolstring(L, 2, &cok->send_buf_len);
			cok->send_buf_need_free = NULL;
		}
		
		if(cok->send_buf_len < 1){
			lua_pushboolean(L, 0);
			lua_pushstring(L, "content empty!");
			return 2;
		}
		
		struct epoll_event ev;
		ev.data.ptr = cok;
		ev.events = EPOLLOUT;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cok->fd, &ev) == -1)
			printf("EPOLL_CTL_MOD error: %d %s", __LINE__, strerror(errno));
		
		add_to_timeout_link(cok, cok->timeout);
	}
	
	return lua_yield(L, 0);
}


int lua_co_read_(cosocket_t *cok){
	if(cok->total_buf_len < 1){
		if(cok->status == 0){
			lua_pushnil(cok->L);
			lua_pushstring(cok->L, "Not connected!");
			return 2;
		}
		return 0;
	}
	size_t be_copy = cok->buf_read_len;
	if(cok->buf_read_len == -1){ // read line
		int i = 0;
		int oi = 0;
		int has = 0;
		cosocket_link_buf_t *nbuf = cok->read_buf;
		while(nbuf){
			for(i = 0; i < nbuf->buf_len; i++){
				if(nbuf->buf[i] == '\n'){
					has = 1;
					break;
				}
			}
			if(has==1)break;
			oi += i;
			nbuf = nbuf->next;
		}
		i += oi;
		
		if(has == 1){
			i+=1;
			be_copy = i;
		}else return 0;
	}else if(cok->buf_read_len == -2){
		be_copy = cok->total_buf_len;
	}
	
	if(cok->status == 0){
//printf("%d %d\n", be_copy, cok->total_buf_len);
		if(be_copy > cok->total_buf_len)
			be_copy = cok->total_buf_len;
	}

	int kk = 0;
	if(be_copy > 0 && cok->total_buf_len >= be_copy){
//printf("lua read %ld, has: %ld last:%d\n", be_copy, cok->total_buf_len, cok->last_buf->buf_len);
		//luaL_Buffer b;luaL_buffinit(cok->L, &b);
		char *buf2lua = large_malloc(be_copy);
		if(!buf2lua){printf("error malloc\n");exit(1);}
		size_t copy_len = be_copy;
		size_t copy_ed = 0;
		int this_copy_len = 0;
		cosocket_link_buf_t *bf = NULL;
		
		while(cok->read_buf){
			this_copy_len = (cok->read_buf->buf_len+copy_ed > copy_len ? copy_len - copy_ed : cok->read_buf->buf_len);
			//luaL_addlstring(&b, cok->read_buf->buf, this_copy_len);
			if(this_copy_len > 0){
				memcpy(buf2lua+copy_ed, cok->read_buf->buf, this_copy_len);
				copy_ed += this_copy_len;
				
				memmove(cok->read_buf->buf, cok->read_buf->buf+this_copy_len, cok->read_buf->buf_len - this_copy_len);
				cok->read_buf->buf_len -= this_copy_len;
			}
//printf("%d %d %ld\n", cok->read_buf->buf_len, this_copy_len, be_copy+(4096-be_copy%4096));
//printf("total %d copy_ed %d\n", cok->total_buf_len, this_copy_len);
			if(copy_ed >= be_copy){ /// not empty
				cok->total_buf_len -= copy_ed;
//printf("push %d\n", be_copy);
				//luaL_pushresult(&b);
				if(cok->buf_read_len == -1){ /// read line , cut the \r \n
					if(buf2lua[be_copy-1] == '\n')be_copy-=1;
					if(buf2lua[be_copy-1] == '\r')be_copy-=1;
				}

				lua_pushlstring(cok->L, buf2lua, be_copy);
				free(buf2lua);

				return 1;
			}else{
				bf = cok->read_buf;
				cok->read_buf = cok->read_buf->next;
				if(cok->last_buf == bf)cok->last_buf = NULL;
				free(bf->buf);
				free(bf);
			}
			
		}
		free(buf2lua);
	}

	return 0;
}

static int lua_co_read(lua_State *L){
	{
		if(!lua_isuserdata(L, 1)){
			lua_pushnil(L);
			lua_pushstring(L, "Error params!");
			return 2;
		}
		
		cosocket_t *cok = (cosocket_t *)lua_touserdata(L, 1);
		if((cok->status != 2 || cok->fd == -1) && cok->total_buf_len < 1){
			lua_pushnil(L);
			lua_pushfstring(L, "Not connected! %d %d", cok->status, cok->fd);
			return 2;
		}
		cok->L = L;
		
		cok->buf_read_len = -2; /// read line
		if(lua_isnumber(L, 2)){
			cok->buf_read_len = lua_tonumber(L, 2);
			if(cok->buf_read_len < 0){
				cok->buf_read_len = 0;
				lua_pushnil(L);
				lua_pushstring(L, "Error params!");
				return 2;
			}
		}else{
			if(lua_isstring(L, 2)){
				if(strcmp("*l", lua_tostring(L, 2)) == 0)
					cok->buf_read_len = -1; /// read all
			}
		}
		
		int rt = lua_co_read_(cok);
		if(rt > 0){
			return rt; // has buf
		}
		
		if(cok->fd == -1){
			lua_pushnil(L);
			lua_pushstring(L, "Not connected!");
			return 2;
		}
		
		if(cok->in_read_action != 1){
			cok->in_read_action = 1;

			struct epoll_event ev;
			ev.data.ptr = cok;
			ev.events = EPOLLIN;
			if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cok->fd, &ev) == -1)
				printf("EPOLL_CTL_MOD error: %d %s", __LINE__, strerror(errno));
		}
		add_to_timeout_link(cok, cok->timeout);
	}
	
	return lua_yield(L, 0);
}

static int _lua_co_close(lua_State *L, cosocket_t *cok){
	if(cok->read_buf){
		cosocket_link_buf_t *fr = cok->read_buf;
		cosocket_link_buf_t *nb = NULL;
		while(fr){
			nb = fr->next;
			free(fr->buf);
			free(fr);
			fr = nb;
		}
		cok->read_buf = NULL;
	}
	if(cok->send_buf_need_free){
		free(cok->send_buf_need_free);
		cok->send_buf_need_free = NULL;
	}
	
	cok->status = 0;
	
	if(cok->fd > -1){
		struct epoll_event ev;
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cok->fd, &ev);
	}

	close(cok->fd);
	cok->fd = -1;
}

static int lua_co_close(lua_State *L){
	if(!lua_isuserdata(L, 1)){
		lua_pushnil(L);
		lua_pushstring(L, "Error params!");
		return 2;
	}
	
	cosocket_t *cok = (cosocket_t *)lua_touserdata(L, 1);
	if(cok->status != 2){
		lua_pushnil(L);
		lua_pushstring(L, "Not connected!");
		return 2;
	}
	
	_lua_co_close(L, cok);
	
	return 0;
}
static int lua_co_gc(lua_State *L){
	cosocket_t *cok = (cosocket_t *)lua_touserdata(L, 1);
	_lua_co_close(L, cok);
//printf("0x%x gc\n", cok);
	return 0;
}

int lua_co_getreusedtimes(lua_State *L){
	lua_pushnumber(L, 0);
	return 1;
}
int lua_co_settimeout(lua_State *L){
	if(!lua_isuserdata(L, 1) || !lua_isnumber(L, 2)){
		lua_pushnil(L);
		lua_pushstring(L, "Error params!");
		return 2;
	}

	cosocket_t *cok = (cosocket_t *)lua_touserdata(L, 1);
	cok->timeout = lua_tonumber(L, 2);
	
	lua_pushboolean(L, 1);
	return 1;
}
int lua_co_setkeepalive(lua_State *L){
	lua_pushnumber(L, 0);
	return 1;
}


/* This is luaL_setfuncs() from Lua 5.2 alpha */
static void setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
	luaL_checkstack(L, nup, "too many upvalues");
	for (; l && l->name; l++) {  /* fill the table with given functions */
		int i = 0;
		for (i = 0; i < nup; i++)  /* copy upvalues to the top */
		  lua_pushvalue(L, -nup);
		lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
		lua_setfield(L, -(nup + 2), l->name);
	}
	lua_pop(L, nup);  /* remove upvalues */
}
/* End of luaL_setfuncs() from Lua 5.2 alpha */

static const luaL_reg M[] =
{
	{"connect", lua_co_connect},
	{"send", lua_co_send},
	{"read", lua_co_read},
	{"receive", lua_co_read},
	{"settimeout", lua_co_settimeout},
	{"setkeepalive", lua_co_setkeepalive},
	{"getreusedtimes", lua_co_getreusedtimes},

	{"close", lua_co_close},
	{"__gc", lua_co_gc},

	{NULL, NULL}
};

static int lua_co_tcp(lua_State *L){
	luaL_checkstack(L, 1, "not enough stack to create connection");
	cosocket_t *cok = NULL;
	cok = (cosocket_t*)lua_newuserdata(L, sizeof(cosocket_t));
	cok->L = L;
	cok->ref = 0;
	cok->fd = -1;
	cok->status = 0;
	cok->read_buf = NULL;
	cok->send_buf_need_free = NULL;
	cok->total_buf_len = 0;
	cok->buf_read_len = 0;
	cok->timeout = 30000;
	cok->dns_query_fd = -1;
	cok->in_read_action = 0;

	if (luaL_newmetatable(L, "cosocket"))
	{
		/* Module table to be set as upvalue */
		luaL_checkstack(L, 1, "not enough stack to register connection MT");

		lua_pushvalue(L, lua_upvalueindex(1));
		setfuncs(L, M, 1);

		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
	}

	lua_setmetatable(L, -2);
}

int swop_counter = 0;
cosocket_swop_t *swop_top = NULL;
cosocket_swop_t *swop_lat = NULL;

int lua_f_coroutine_wait(lua_State *L){
	{
		if(!lua_isthread(L, 1))return 0;
		
		lua_State *JL = lua_tothread(L, 1);
		if(lua_status(JL) != LUA_YIELD){
			/// get returns to tmp table (may have)
			int rts = lua_gettop(JL);
			lua_xmove(JL, L, rts);
			int ret = lua_resume(JL, 0);
			if (ret == LUA_ERRRUN) {lua_pop(JL, -1);}
			return rts;
		}
		
		char key[32];
		sprintf(key,"%x__be_resume", JL);
		lua_pushlightuserdata(JL, L);
		lua_setglobal(JL, key);
		lua_pop(L, 1);
	}
	return lua_yield(L, 0);
}
int lua_f_coroutine_resume_waiting(lua_State *L){
	char key[32];
	sprintf(key,"%x__be_resume", L);
	lua_getglobal(L, key);
	if(LUA_TLIGHTUSERDATA == lua_type(L, -1)){
		
		cosocket_swop_t *swop = malloc(sizeof(cosocket_swop_t));
		if(swop == NULL){
			exit(1);
		}
		swop->L = L;
		swop->next = NULL;
		if(swop_lat != NULL){
			swop_lat->next = swop;
		}else{
			swop_top->next = swop;
		}
		swop_lat = swop;
			
		lua_State *_L = lua_touserdata(L, -1);
		
		lua_pushnil(L);
		lua_setglobal(L, key);
		
		lua_pop(L, 1);

		if(lua_status(_L) == LUA_YIELD){
			int rts = lua_gettop(L);
			lua_xmove(L, _L, rts);
			int ret = lua_resume(_L, rts);
			if (ret == LUA_ERRRUN) {
				printf("%d isstring: %s\n", __LINE__, lua_tostring(_L, -1));
				lua_pop(_L, -1);
			}
	
		}
	}

	return 0;
}

int lua_f_coroutine_swop(lua_State *L){
	if(swop_counter++ < 100){
		lua_pushboolean(L, 0);
		return 1;
	}
	cosocket_swop_t *swop = malloc(sizeof(cosocket_swop_t));
	if(swop == NULL){
		lua_pushboolean(L, 0);
		return 1;
	}
	
	swop_counter = 0;
	
	swop->L = L;
	swop->next = NULL;
	
	if(swop_lat != NULL){
		swop_lat->next = swop;
	}else{
		swop_top->next = swop;
	}
	swop_lat = swop;

	return lua_yield(L, 0);
}

lua_State *job_L = NULL;
int io_counts = 0;
int epoll_worker(){
	//printf("epoll_worker\n");
	struct epoll_event ev,events[128];
	int n = 0;
	int nfds = 0, i = 0;
	time_t chk_time=time(NULL);
	cosocket_t *cok = NULL;
	while(1){
		if(lua_status(job_L) != LUA_YIELD)break;
		nfds = epoll_wait(epoll_fd, events, 128, 10);
		io_counts+=nfds;
		swop_counter = swop_counter/2;
		
		for(i=0; i<nfds; i++){
			cok = events[i].data.ptr;
			if(cok->dns_query_fd > -1){ /// is dns query
				unsigned char pkt[2048];
				int n;

				while ((n = recvfrom(cok->dns_query_fd, pkt, sizeof(pkt), 0, NULL, NULL)) > 0 && n > sizeof(dns_query_header_t)){
					parse_dns_result(epoll_fd, cok->dns_query_fd, cok, pkt, n);
					break;
				}
				close(cok->dns_query_fd);
				cok->dns_query_fd = -1;
				continue;
			}
			if(events[i].events & EPOLLIN){
init_read_buf:
				if(!cok->read_buf || (cok->last_buf->buf_len >= cok->last_buf->buf_size)){/// init read buf
					cosocket_link_buf_t *nbuf = NULL;
					nbuf = malloc(sizeof(cosocket_link_buf_t));
					nbuf->buf = malloc(4096);
					nbuf->buf_size = 4096;
					nbuf->buf_len = 0;
					nbuf->next = NULL;
					
					if(cok->read_buf)
						cok->last_buf->next = nbuf;
					else
						cok->read_buf = nbuf;
					
					cok->last_buf = nbuf;
				}
				
				while((n = recv(cok->fd, cok->last_buf->buf+cok->last_buf->buf_len, cok->last_buf->buf_size-cok->last_buf->buf_len, 0)) > 0){
					cok->last_buf->buf_len += n;
					cok->total_buf_len += n;
					if(cok->last_buf->buf_len >= cok->last_buf->buf_size)goto init_read_buf;
				}
				
				if(n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)){
					if(!del_in_timeout_link(cok)){// && cok->in_read_action == 1
						//printf("del error %d\n", __LINE__);
						//exit(1);
					}

					{
						cok->status = 0;
						if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cok->fd, &ev) == -1)
							printf("EPOLL_CTL_MOD error: %d %s", __LINE__, strerror(errno));
						close(cok->fd);
						cok->fd = -1;
						cok->status = 0;
					}
					if(cok->in_read_action == 1){
						cok->in_read_action = 0;
						int ret = 0;
						int rt = lua_co_read_(cok);
						if(rt > 0){
							ret = lua_resume(cok->L, rt);
						}else if(n == 0){
							lua_pushnil(cok->L);
							ret = lua_resume(cok->L, 1);
						}
						if (ret == LUA_ERRRUN && lua_isstring(cok->L, -1)) {
							printf("%d isstring: %s\n", __LINE__, lua_tostring(cok->L, -1));
							//lua_pop(cok->L, -1);
							{
								epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cok->fd, &ev);
								close(cok->fd);
								cok->fd = -1;
								cok->status = 0;
							}
							
							if(lua_gettop(cok->L) > 1){
								lua_replace(cok->L, 2);
								lua_pushnil(cok->L);
								lua_replace(cok->L, 1);
								lua_settop(cok->L, 2);
							}else{
								lua_pushnil(cok->L);
								lua_replace(cok->L, 1);
							}
							lua_f_coroutine_resume_waiting(cok->L);
						}
					}
				}else{
					if(cok->in_read_action == 1){
						int rt = lua_co_read_(cok);
						if(rt > 0){
							cok->in_read_action = 0;
							if(!del_in_timeout_link(cok)){
								printf("del error %d\n", __LINE__);
								exit(1);
							}
							/*ev.data.ptr = cok;
							ev.events = EPOLLPRI;
							if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cok->fd, &ev) == -1)
								printf("EPOLL_CTL_MOD error: %d %s", __LINE__, strerror(errno));*/

							int ret = lua_resume(cok->L, rt);
							if (ret == LUA_ERRRUN && lua_isstring(cok->L, -1)) {
								printf("%d isstring: %s\n", __LINE__, lua_tostring(cok->L, -1));
								//lua_pop(cok->L, -1);
								{
									epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cok->fd, &ev);
									close(cok->fd);
									cok->fd = -1;
									cok->status = 0;
								}
								
								if(lua_gettop(cok->L) > 1){
									lua_replace(cok->L, 2);
									lua_pushnil(cok->L);
									lua_replace(cok->L, 1);
									lua_settop(cok->L, 2);
								}else{
									lua_pushnil(cok->L);
									lua_replace(cok->L, 1);
								}
								
								lua_f_coroutine_resume_waiting(cok->L);
							}
						}
					}//else{printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!aaaaa\n");}
				}
			}else if(events[i].events & EPOLLOUT){
				if(cok->status == 1){
					cok->status = 2;
					int result = 0;
					socklen_t result_len = sizeof(result);
					if (getsockopt(cok->fd, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0)
						continue;
					if(!del_in_timeout_link(cok)){
						printf("del error %d  fd %d\n", __LINE__, cok->fd);
						exit(1);
					}
					if (result != 0) {
						{
							epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cok->fd, &ev);
//printf("0x%x close fd %d   l:%d\n", cok->L, cok->fd, __LINE__);
							close(cok->fd);
							cok->fd = -1;
							cok->status = 0;
						}
						lua_pushnil(cok->L);
						lua_pushstring(cok->L, "Connect error!");
						int ret = lua_resume(cok->L, 2);
						if (ret == LUA_ERRRUN && lua_isstring(cok->L, -1)) {
							printf("%d isstring: %s\n", __LINE__, lua_tostring(cok->L, -1));
							//lua_pop(cok->L, -1);
							if(lua_gettop(cok->L) > 1){
								lua_replace(cok->L, 2);
								lua_pushnil(cok->L);
								lua_replace(cok->L, 1);
								lua_settop(cok->L, 2);
							}else{
								lua_pushnil(cok->L);
								lua_replace(cok->L, 1);
							}
							lua_f_coroutine_resume_waiting(cok->L);
						}
					}else{ /// connected
						cok->in_read_action = 0;
						{
							ev.data.ptr = cok;
							ev.events = EPOLLPRI;
							if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cok->fd, &ev) == -1)
								printf("EPOLL_CTL_MOD error: %d %s", __LINE__, strerror(errno));
						}
						lua_pushboolean(cok->L, 1);
						int ret = lua_resume(cok->L, 1);
						if (ret == LUA_ERRRUN && lua_isstring(cok->L, -1)) {
							printf("%d isstring: %s\n", __LINE__, lua_tostring(cok->L, -1));
							//lua_pop(cok->L, -1);
							if(lua_gettop(cok->L) > 1){
								lua_replace(cok->L, 2);
								lua_pushnil(cok->L);
								lua_replace(cok->L, 1);
								lua_settop(cok->L, 2);
							}else{
								lua_pushnil(cok->L);
								lua_replace(cok->L, 1);
							}
							lua_f_coroutine_resume_waiting(cok->L);
						}
					}
				}else{
					cok->in_read_action = 0;
					while((n = send(cok->fd, cok->send_buf+cok->send_buf_ed, cok->send_buf_len-cok->send_buf_ed, MSG_DONTWAIT | MSG_NOSIGNAL)) > 0){
						cok->send_buf_ed += n;
					}
					if(cok->send_buf_ed == cok->send_buf_len || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)){
						if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK){
							ev.data.ptr = cok;
							ev.events = EPOLLPRI;
							if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cok->fd, &ev) == -1)
								printf("EPOLL_CTL_MOD error: %d %s", __LINE__, strerror(errno));
							close(cok->fd);
							cok->fd = -1;
							cok->status = 0;
							cok->send_buf_ed = 0;
						}else{
							ev.data.ptr = cok;
							ev.events = EPOLLIN;
							if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cok->fd, &ev) == -1)
								printf("EPOLL_CTL_MOD error: %d %s", __LINE__, strerror(errno));
						}
						{
							if(cok->send_buf_need_free){
								free(cok->send_buf_need_free);
								cok->send_buf_need_free = NULL;
							}
						}
						
						if(!del_in_timeout_link(cok)){
							printf("del error %d\n", __LINE__);
							exit(1);
						}

						if(cok->send_buf_ed == cok->send_buf_len)
							lua_pushnumber(cok->L, cok->send_buf_ed);
						else if(cok->fd == -1)
							lua_pushnil(cok->L);
						else
							lua_pushboolean(cok->L, 0);
						int ret = lua_resume(cok->L, 1);
						if (ret == LUA_ERRRUN && lua_isstring(cok->L, -1)) {
							printf("%d isstring: %s\n", __LINE__, lua_tostring(cok->L, -1));
							//lua_pop(cok->L, -1);
							if(lua_gettop(cok->L) > 1){
								lua_replace(cok->L, 2);
								lua_pushnil(cok->L);
								lua_replace(cok->L, 1);
								lua_settop(cok->L, 2);
							}else{
								lua_pushnil(cok->L);
								lua_replace(cok->L, 1);
							}
							lua_f_coroutine_resume_waiting(cok->L);
						}
					}
				}
			}else if(events[i].events & EPOLLPRI){
printf("fd:%d EPOLLPRI\n", cok->fd);
			}else{
printf("fd:%d other\n", cok->fd);
				del_in_timeout_link(cok);
				{
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cok->fd, &ev);
					close(cok->fd);
					cok->fd = -1;
					cok->status = 0;
				}

				lua_pushnil(cok->L);
				lua_pushstring(cok->L, "Connect error!");
				int ret = lua_resume(cok->L, 2);
				if (ret == LUA_ERRRUN && lua_isstring(cok->L, -1)) {
					printf("%d isstring: %s\n", __LINE__, lua_tostring(cok->L, -1));
					//lua_pop(cok->L, -1);
					if(lua_gettop(cok->L) > 1){
						lua_replace(cok->L, 2);
						lua_pushnil(cok->L);
						lua_replace(cok->L, 1);
						lua_settop(cok->L, 2);
					}else{
						lua_pushnil(cok->L);
						lua_replace(cok->L, 1);
					}
					lua_f_coroutine_resume_waiting(cok->L);
				}
			}
		}

		if(io_counts >= 1000){
			io_counts = 0;
			if(clearthreads_handler != 0){
				lua_rawgeti(LM, LUA_REGISTRYINDEX, clearthreads_handler); 
				if(lua_pcall(LM, 0, 0, 0)){
					printf("%d isstring: %s\n", __LINE__, lua_tostring(LM, -1));
					lua_pop(LM, -1);
					exit(0);
				}
			}
		}
		
		time(&timer);
		if(timer-chk_time > 0){
			chk_time = timer;
			chk_do_timeout_link(epoll_fd);
		}
		/// resume swops
		{
			cosocket_swop_t *swop = NULL;
			if(swop_top->next != NULL){
				swop = swop_top->next;
				swop_top->next = swop->next;
				if(swop_top->next == NULL)swop_lat = NULL;
				lua_State *L = swop->L;
				free(swop);
				swop = NULL;

				lua_pushboolean(L, 1);
				int ret = lua_resume(L, 1);
				if (ret == LUA_ERRRUN && lua_isstring(L, -1)) {
					//printf("%d isstring: %s\n", __LINE__, lua_tostring(L, -1));
					lua_pop(L, -1);
					lua_f_coroutine_resume_waiting(L);
				}
			}
		}
	}
}


static const struct luaL_reg cosocket_methods[] =
{
	{ "tcp", lua_co_tcp },
	{ NULL, NULL }
};
/*
static int main(int argc, char *argv[])
{
	epoll_fd = epoll_create(128);
	
	swop_top = malloc(sizeof(cosocket_swop_t));
	swop_top->next = NULL;
	
	lua_State *L = luaL_newstate();
	LM = L;
	lua_gc(L,LUA_GCSTOP,0); 
	luaL_openlibs(L); /* Load Lua libraries * /
	lua_gc(L,LUA_GCRESTART,0);
	
	lua_setglobal(L, "null");
	lua_register(L, "coroutine_wait", lua_f_coroutine_wait);
	lua_register(L, "coroutine_resume_waiting", lua_f_coroutine_resume_waiting);
	lua_register(L, "swop", lua_f_coroutine_swop);
	lua_register(L, "sha1bin", lua_f_sha1bin);
	lua_register(L, "base64encode", lua_f_base64encode);
	lua_register(L, "base64decode", lua_f_base64decode);
	lua_register(L, "escape_uri", lua_f_escape_uri);
	lua_register(L, "unescape_uri", lua_f_unescape_uri);
	lua_register(L, "time", lua_f_time);
	lua_register(L, "longtime", lua_f_longtime);
	
	
	static const struct luaL_reg _MT[] = {{NULL, NULL}};
	luaL_openlib(L, "cosocket", _MT, 0);
	if (luaL_newmetatable(L, "cosocket*"))
	{
		luaL_register(L, NULL, _MT);
		lua_pushliteral(L, "cosocket*");
		lua_setfield(L, -2, "__metatable");
	}
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	setfuncs(L, cosocket_methods, 1);
	lua_State *_L = lua_newthread(L);
	luaL_loadfile(_L, "main.lua");
	
	if(lua_resume(_L, 0) == LUA_ERRRUN)
		printf("Error Msg is %s.\n",lua_tostring(_L,-1));
	
	lua_getglobal(LM, "clearthreads");
	clearthreads_handler = luaL_ref(L , LUA_REGISTRYINDEX);
	
	/*
	cosocket_t a;
	a.dns_query_fd = -1;
	do_dns_query(epoll_fd, &a, "www.163.com");
	
	cosocket_t b;
	b.dns_query_fd = -1;
	do_dns_query(epoll_fd, &b, "www.upyunsa.com");* /
	
	epoll_worker();
	return 0;
}
*/

int lua_f_startloop(lua_State *L){
	luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1), 1,
	"Lua function expected");
	
	job_L = lua_newthread(L);
	
	lua_pushvalue(L, 1);  /* move function to top */
	lua_xmove(L, job_L, 1);  /* move function from L to job_L */

	lua_resume(job_L, 0);
	
	lua_getglobal(job_L, "clearthreads");
	clearthreads_handler = luaL_ref(job_L, LUA_REGISTRYINDEX);
	LM = job_L;
	epoll_worker();

	return 0;
}

int luaopen_coevent(lua_State *L) {
	epoll_fd = epoll_create(128);
	
	swop_top = malloc(sizeof(cosocket_swop_t));
	swop_top->next = NULL;
	
	lua_setglobal(L, "null");
	lua_register(L, "startloop", lua_f_startloop);
	lua_register(L, "coroutine_wait", lua_f_coroutine_wait);
	lua_register(L, "coroutine_resume_waiting", lua_f_coroutine_resume_waiting);
	lua_register(L, "swop", lua_f_coroutine_swop);
	lua_register(L, "sha1bin", lua_f_sha1bin);
	lua_register(L, "base64encode", lua_f_base64encode);
	lua_register(L, "base64decode", lua_f_base64decode);
	lua_register(L, "escape_uri", lua_f_escape_uri);
	lua_register(L, "unescape_uri", lua_f_unescape_uri);
	lua_register(L, "time", lua_f_time);
	lua_register(L, "longtime", lua_f_longtime);
	
	luaL_loadstring(L, "local _coresume = coroutine.resume \
local _cocreate = coroutine.create \
coroutine.resume = nil \
coroutine.create = nil \
local allthreads = {} \
function clearthreads() \
	local v \
	local c = 0 \
	for v in pairs(allthreads) do \
		if coroutine.status(v) ~= 'suspended' then \
			allthreads[v] = nil \
		else c=c+1 \
		end \
	end \
	collectgarbage() \
end \
function newthread(f,n1,n2,n3,n4,n5) local F = _cocreate(function(n1,n2,n3,n4,n5) local R = {f(n1,n2,n3,n4,n5)} coroutine_resume_waiting(unpack(R)) return unpack(R) end) _coresume(F,n1,n2,n3,n4,n5) allthreads[F]=1 return F end");//allthreads[F] = {...} 
	lua_pcall(L,0,0,0);
	
	static const struct luaL_reg _MT[] = {{NULL, NULL}};
	luaL_openlib(L, "cosocket", _MT, 0);
	if (luaL_newmetatable(L, "cosocket*"))
	{
		luaL_register(L, NULL, _MT);
		lua_pushliteral(L, "cosocket*");
		lua_setfield(L, -2, "__metatable");
	}
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	setfuncs(L, cosocket_methods, 1);
	
	lua_pushcfunction(L, lua_f_startloop);

	return 1;
}