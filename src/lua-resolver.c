#include "coevent.h"

static unsigned char temp_buf[4096] = {0};
static uint16_t dns_tid = 0;

int lua_co_build_dns_query_packet(lua_State *L)
{
    size_t nlen = 0;
    int n = 0, i = 0;
    dns_query_header_t *header = NULL;
    char *name = NULL, *s = NULL, *p = NULL, *client_ip = NULL;

    name = lua_tolstring(L, 1, &nlen);

    if(lua_gettop(L) > 1) {
        client_ip = lua_tostring(L, 2);
    }

    header           = (dns_query_header_t *) temp_buf;

    if(++dns_tid > 65535 - 1) {
        dns_tid = 1;
    }

    header->tid      = htons(dns_tid);
    header->flags    = htons(0x100);
    header->nqueries = htons(1);
    header->nanswers = 0;
    header->nauth    = 0;
    header->nother   = 0;
    // Encode DNS name
    p = (char *) &header->data;   /* For encoding host name into packet */

    do {
        if((s = strchr(name, '.')) == NULL) {
            s = name + nlen;
        }

        n = s - name;           /* Chunk length */
        *p++ = n;               /* Copy length */

        for(i = 0; i < n; i++) {    /* Copy chunk */
            *p++ = name[i];
        }

        if(*s == '.') {
            n++;
        }

        name += n;
        nlen -= n;
    } while(*s != '\0');

    *p++ = 0;           /* Mark end of host name */
    *p++ = 0;           /* Well, lets put this byte as well */
    *p++ = 1;           /* Query Type */
    *p++ = 0;
    *p++ = 1;           /* Class: inet, 0x0001 */
    n = (unsigned char *) p - temp_buf;      /* Total packet length */

    if(client_ip) {
        temp_buf[11] = 1;   /* set additional count to  1 */

        p = temp_buf + n; /* p points to additional section */

        *p++ = 0;      /* root name */

        *p++ = 0;      /* OPT */
        *p++ = 41;

        *p++ = 16;      /* UDP payload size: 1024 */
        *p++ = 0;

        *p++ = 0;      /* extended RCODE and flags */
        *p++ = 0;
        *p++ = 128;
        *p++ = 0;

        *p++ = 0;      /* edns-client-subnet's length */
        *p++ = 12;

        /* edns-client-subnet */
        *p++ = 0;      /* option code: 8 */
        *p++ = 8;

        *p++ = 0;      /* option length: 8 */
        *p++ = 8;

        *p++ = 0;      /* family: 1 */
        *p++ = 1;

        *p++ = 18;     /* source netmask: 32 */
        *p++ = 0;      /* scope netmask: 0 */

        in_addr_t addr = inet_addr(client_ip);  /* client subnet */
        char *q = (char *) &addr;

        *p++ = *q++;
        *p++ = *q++;
        *p++ = *q++;
        *p++ = 0;

        n += 23;
    }

    lua_pushlstring(L, temp_buf, n);

    return 1;
}

#define _NTOHS(p) (((p)[0] << 8) | (p)[1])
int lua_co_parse_dns_result(lua_State *L)
{
    size_t len = 0;
    char *data = lua_tolstring(L, 1, &len);
    lua_settop(L, 0);

    const unsigned char *p = NULL, *e = NULL;
    dns_query_header_t *header = NULL;
    uint16_t type = 0;
    int found = 0, stop = 0, dlen = 0, nlen = 0;
    int err = 0;
    header = (dns_query_header_t *) data;

    if(ntohs(header->nqueries) != 1) {
        err = 1;
    }

    header->tid = ntohs(header->tid);

    if(header->tid != dns_tid) {
        err = 1;
    }


    /* Skip host name */
    if(err == 0) {
        //static char hostname[1024] = {0};
        //int hostname_len = 0;

        for(e = data + len, nlen = 0, p = &header->data[0]; p < e
            && *p != '\0'; p++) {
            //hostname[hostname_len++] = (*p == 3 ? '.' : *p);
            nlen++;
        }

        //hostname[hostname_len] = '\0';
        //printf("%s\n", hostname);
    }

    /* We sent query class 1, query type 1 */
    if(&p[5] > e || _NTOHS(p + 1) != 0x01) {
        err = 1;
    }

    struct in_addr ips[10];

    /* Go to the first answer section */
    if(err == 0) {
        p += 5;

        /* Loop through the answers, we want A type answer */
        for(found = stop = 0; !stop && &p[12] < e;) {
            /* Skip possible name in CNAME answer */
            if(*p != 0xc0) {
                while(*p && &p[12] < e) {
                    p++;
                }

                p--;
            }

            type = htons(((uint16_t *) p) [1]);

            if(type == 5) {
                /* CNAME answer. shift to the next section */
                dlen = htons(((uint16_t *) p) [5]);
                p += 12 + dlen;

            } else if(type == 0x01) {
                dlen = htons(((uint16_t *) p) [5]);
                p += 12;

                if(p + dlen <= e) {
                    memcpy(&ips[found], p, dlen);
                }

                p += dlen;

                if(++found == header->nanswers) {
                    stop = 1;
                }

                if(found >= 10) {
                    break;
                }

            } else {
                stop = 1;
            }
        }
    }

    if(found > 0) {
        lua_pushstring(L, inet_ntoa(ips[0]));

        if(found > 1) {
            lua_createtable(L, found, 0);
            int i = 0;

            for(i = 0; i < found; i++) {
                lua_pushstring(L, inet_ntoa(ips[i]));
                lua_rawseti(L, 2, i + 1);
            }

            return 2;
        }

        return 1;
    }

    return 0;
}
