#include "coevent.h"

static unsigned char sha_buf[SHA_DIGEST_LENGTH];

int lua_f_sha1bin ( lua_State *L )
{
    const unsigned char *src = NULL;
    size_t slen = 0;

    if ( lua_isnil ( L, 1 ) ) {
        src = ( unsigned char * ) "";

    } else {
        src = ( unsigned char * ) luaL_checklstring ( L, 1, &slen );
    }

    SHA_CTX sha;
    SHA1_Init ( &sha );
    SHA1_Update ( &sha, src, slen );
    SHA1_Final ( sha_buf, &sha );
    lua_pushlstring ( L, ( char * ) sha_buf, sizeof ( sha_buf ) );
    return 1;
}
