/* Copyright 2011, 2012 SRI International
 * See LICENSE for other credits and copying information
 */

#include <event2/buffer.h>
#include <curl/curl.h>
#include <vector>
#include <sstream>

using namespace std;

#include "util.h"
#include "connections.h"
#include "protocol.h"
#include "steg.h"
#include "rng.h"

/** here we initiate our payload strategy (it should be)based on the config 
    file so I include all available payload servers. The global object is of
    PayloadServer type though
*/
#include "payload_server.h"
#include "trace_payload_server.h"
#include "apache_payload_server.h"

#include "cookies.h"
#include "base64.h"
#include "b64cookies.h"

#include "http_steg_mods/file_steg.h"
#include "http_steg_mods/swfSteg.h"
#include "http_steg_mods/pdfSteg.h"
#include "http_steg_mods/jsSteg.h"
#include "http_steg_mods/jpgSteg.h"
#include "http_steg_mods/pngSteg.h"
#include "http_steg_mods/gifSteg.h"

#include "http.h"

STEG_DEFINE_MODULE(http);
void
http_steg_config_t::init_http_steg_config_t(bool init_payload_server)
{ 
  if (init_payload_server) {
    string payload_filename;
    if (is_clientside)
      payload_filename = "traces/client.out";
    else
      payload_filename = "traces/server.out";
  
    payload_server = new TracePayloadServer(is_clientside ? client_side : server_side, payload_filename);

    init_file_steg_mods();

  }

}

void http_steg_config_t::init_file_steg_mods()
{
  // we can't call this in constructor cause 
  //it should be called after the payload server is initialized

  //initiating the steg modules
  //TODO: for now the first modules are set to void till their codes be
  //transformed into a FileStegMod child
  file_steg_mods[HTTP_CONTENT_JPEG] = new JPGSteg(payload_server, noise2signal);
  file_steg_mods[HTTP_CONTENT_PNG] = new PNGSteg(payload_server, noise2signal);
  file_steg_mods[HTTP_CONTENT_GIF] = new GIFSteg(payload_server, noise2signal);
  file_steg_mods[HTTP_CONTENT_SWF] = new SWFSteg(payload_server, noise2signal);
  file_steg_mods[HTTP_CONTENT_PDF] = new PDFSteg(payload_server, noise2signal);

}
http_steg_config_t::http_steg_config_t(config_t *cfg)
   : steg_config_t(cfg),
    is_clientside(cfg->mode != LSN_SIMPLE_SERVER)
{
  init_http_steg_config_t(true);

}

http_steg_config_t::http_steg_config_t(config_t *cfg, bool init_payload_server)
   : steg_config_t(cfg),
     is_clientside(cfg->mode != LSN_SIMPLE_SERVER)
{
  init_http_steg_config_t(init_payload_server);
}

http_steg_config_t::~http_steg_config_t()
{
  delete payload_server; //maybe we don't need it
  for(unsigned int i = 0; i < c_no_of_steg_protocol; i++)
    delete file_steg_mods[i];

}

steg_t *
http_steg_config_t::steg_create(conn_t *conn)
{
  return new http_steg_t(this, conn);
}

void evbuffer_dump(struct evbuffer *buf, FILE *out);
void buf_dump(unsigned char* buf, int len, FILE *out);
int gen_uri_field(char* uri, unsigned int uri_sz, char* data, int datalen);

void
evbuffer_dump(struct evbuffer *buf, FILE *out)
{
  int nextent = evbuffer_peek(buf, SSIZE_MAX, 0, 0, 0);
  struct evbuffer_iovec v[nextent];
  int i;
  const unsigned char *p, *limit;

  if (evbuffer_peek(buf, -1, 0, v, nextent) != nextent)
    abort();

  for (i = 0; i < nextent; i++) {
    p = (const unsigned char *)v[i].iov_base;
    limit = p + v[i].iov_len;

    putc('|', out);
    while (p < limit) {
      if (*p < 0x20 || *p >= 0x7F || *p == '\\' || *p == '|')
        fprintf(out, "\\x%02x", *p);
      else
        putc(*p, out);
      p++;
    }
  }
  putc('|', out);
}

void
buf_dump(unsigned char* buf, int len, FILE *out)
{
  int i=0;
  putc('|', out);
  while (i < len) {
    if (buf[i] < 0x20 || buf[i] >= 0x7F || buf[i] == '\\' || buf[i]== '|')
      fprintf(out, "\\x%02x", buf[i]);
    else
      putc(buf[i], out);
    i++;
  }
  putc('|', out);
  putc('\n', out);
}

http_steg_t::http_steg_t(http_steg_config_t *cf, conn_t *cn)
  : config(cf), conn(cn),
    have_transmitted(false), have_received(false)
{
  memset(peer_dnsname, 0, sizeof peer_dnsname);
}

http_steg_t::~http_steg_t()
{
}

steg_config_t *
http_steg_t::cfg()
{
  return config;
}

size_t
http_steg_t::clamp(size_t val, size_t lo, size_t hi)
{
  if (val < lo) return lo;
  if (val > hi) return hi;
  return val;
}

size_t
http_steg_t::transmit_room(size_t pref, size_t lo, size_t hi)
{
  if (have_transmitted)
    /* can't send any more on this connection */
    return 0;

  if (config->is_clientside) {
    // MIN_COOKIE_SIZE and MAX_COOKIE_SIZE are *after* base64'ing
    if (lo < MIN_COOKIE_SIZE*3/4)
      lo = MIN_COOKIE_SIZE*3/4;

    if (hi > MAX_COOKIE_SIZE*3/4)
      hi = MAX_COOKIE_SIZE*3/4;
  }
  else {
    if (!have_received) {
      log_debug(conn, "yet have to receive");
      return 0;
    }

    log_debug(conn, "checking available capacity for type %u", type);
    switch (type)
      {
        //TODO: This needs to be handle by the SWFSteg i.e. the
        //default case but because there is no pre-generated
        //swf payloads it needs a bit of tweak
       //case HTTP_CONTENT_SWF:
         //if (hi >= 1024)
           //hi = 1024;
           //break;

      case HTTP_CONTENT_JAVASCRIPT:
        if (hi >= config->payload_server->_payload_database.typed_maximum_capacity(HTTP_CONTENT_JAVASCRIPT) / 2)
          hi = config->payload_server->_payload_database.typed_maximum_capacity(HTTP_CONTENT_JAVASCRIPT) / 2;
        break;

      case HTTP_CONTENT_HTML:
        if (hi >= config->payload_server->_payload_database.typed_maximum_capacity(HTTP_CONTENT_HTML) / 2)
          hi = config->payload_server->_payload_database.typed_maximum_capacity(HTTP_CONTENT_HTML) / 2;
        break;

      //case HTTP_CONTENT_PDF:
        //if (hi >= PDF_MIN_AVAIL_SIZE)
         // hi = PDF_MIN_AVAIL_SIZE;
       // break;

      case HTTP_CONTENT_ENCRYPTEDZIP: //We need to prevent thi
        return 0;

      default:
        hi = config->payload_server->_payload_database.typed_maximum_capacity(type);
      }
        
  }

  if (hi < lo) { 
    /* cannot satisfy this request, doesn't make sense */
    log_warn("hi<lo: client=%d type=%d hi=%ld lo=%ld",
              config->is_clientside, type,
              (unsigned long)hi, (unsigned long)lo);
    //this might because of the max size of the connection
    //type can not handle that much data. so we return 0
    //so the pick_connection finds another connection
    return 0;
  }

  return clamp(pref + rng_range_geom(hi - lo + 1, 8), lo, hi); //vmon I've added the randomness
  //but don't know why, (I think zack took it away) also I would get into  
  //[error] rng_range_geom: assertion failure at src/rng.cc:202: 0 < xv && xv < hi
  // that is why added that + 1 there

}

int
lookup_peer_name_from_ip(const char* p_ip, char* p_name)  
{
  struct addrinfo* ailist;
  struct addrinfo* aip;
  struct addrinfo hint;
  int res;
  char buf[128];

  hint.ai_flags = AI_CANONNAME;
  hint.ai_family = PF_UNSPEC;
  hint.ai_socktype = 0;
  hint.ai_protocol = 0;
  hint.ai_addrlen = 0;
  hint.ai_canonname = NULL;
  hint.ai_addr = NULL;
  hint.ai_next = NULL;

  strcpy(buf, p_ip);
  buf[strchr(buf, ':') - buf] = 0;

  if ((res = getaddrinfo(buf, NULL, &hint, &ailist))) {
    log_warn("getaddrinfo(%s) failed: %s", p_ip, gai_strerror(res));
    return 0;
  }

  for (aip = ailist; aip != NULL; aip = aip->ai_next) {
    char buf[512];
    if (getnameinfo(aip->ai_addr, sizeof(struct sockaddr),
        buf, 512, NULL, 0, 0) == 0) {
      strcpy(p_name, buf);
      return 1;
    }
  }

  return 0;
}

int
http_steg_t::http_client_cookie_transmit (evbuffer *source, conn_t *conn)
{
  struct evbuffer *dest = conn->outbound();
  size_t sbuflen = evbuffer_get_length(source);
  int bufsize = 10000;
  char* buf = (char*) xmalloc(bufsize);

  char* data;
  char* data2 = (char*) xmalloc (sbuflen*4);
  char* cookiebuf = (char*) xmalloc (sbuflen*8);
  size_t payload_len = 0;
  size_t cnt = 0;
  size_t cookie_len = 0;
  size_t rval;
  size_t len = 0;
  int transmit_len = 0;
  // '+' -> '-', '/' -> '_', '=' -> '.' per
  // RFC4648 "Base 64 encoding with RL and filename safe alphabet"
  // (which does not replace '=', but dot is an obvious choice; for
  // this use case, the fact that some file systems don't allow more
  // than one dot in a filename is irrelevant).
  base64::encoder E(false, '-', '_', '.');

  data = (char*) evbuffer_pullup(source, sbuflen);
  if (!data) {
    log_debug("evbuffer_pullup failed");
    goto err;
  }

  // retry up to 10 times
  while (!payload_len) {
    payload_len = config->payload_server->find_client_payload(buf, bufsize,
                                      TYPE_HTTP_REQUEST);
    if (cnt++ == 10) {
      goto err;
    }
  }
  buf[payload_len] = 0;

  if (peer_dnsname[0] == '\0')
    lookup_peer_name_from_ip(conn->peername, peer_dnsname);

  memset(data2, 0, sbuflen*4);
  len  = E.encode(data, sbuflen, data2);
  len += E.encode_end(data2+len);

  cookie_len = gen_b64_cookies(cookiebuf, data2, len);
  cookiebuf[cookie_len] = 0;

  log_debug(conn, "cookie input %lu encoded %lu final %lu/%lu",
            (unsigned long)sbuflen, (unsigned long)len,
            (unsigned long)cookie_len, (unsigned long)strlen(cookiebuf));
  log_debug(conn, "cookie encoded: %s", data2);
  log_debug(conn, "cookie final: %s", cookiebuf);

  // add uri field
  rval = evbuffer_add(dest, buf, strstr(buf, "\r\n") - buf + 2);
  if (rval) {
    log_warn("error adding uri field\n");
    goto err;
  }
  transmit_len += strstr(buf, "\r\n") - buf + 2;

  rval = evbuffer_add(dest, "Host: ", 6);
  if (rval) {
    log_warn("error adding host field\n");
    goto err;
  }
  transmit_len += 6;

  rval = evbuffer_add(dest, peer_dnsname, strlen(peer_dnsname));
  if (rval) {
    log_warn("error adding peername field\n");
    goto err;
  }
  transmit_len += strlen(peer_dnsname);

  rval = evbuffer_add(dest, strstr(buf, "\r\n"), payload_len - (unsigned int) (strstr(buf, "\r\n") - buf));
  if (rval) {
    log_warn("error adding HTTP fields\n");
    goto err;
  }
  transmit_len +=  strstr(buf, "\r\n") - buf;
  
  rval =   evbuffer_add(dest, "Cookie: ", 8);
  if (rval) {
    log_warn("error adding cookie fields\n");
    goto err;
  }
  transmit_len += 8;

  rval = evbuffer_add(dest, cookiebuf, cookie_len);
  if (rval) {
    log_warn("error adding cookie buf\n");
    goto err;
  }
  transmit_len += cookie_len;

  rval = evbuffer_add(dest, "\r\n\r\n", 4);
  if (rval) {
    log_warn("error adding terminators \n");
    goto err;
  }
  transmit_len += 4;

  evbuffer_drain(source, sbuflen);
  log_debug("CLIENT TRANSMITTED payload %d\n", (int) sbuflen);
  conn->cease_transmission();

  type = config->payload_server->find_uri_type(buf, bufsize);
  have_transmitted = true;

  free(buf);
  free(data2);
  return transmit_len;

err:
  free(buf);
  free(data2);
  return -1;

}

int gen_uri_field(char* uri, unsigned int uri_sz, char* data, int datalen) {
  unsigned int so_far = 0;
  uri[0] = 0;

  strcat(uri, "GET /");
  so_far = 5;

  while (datalen > 0) {
    unsigned int r = rand() % 4;

    if (r == 1) {
      r = rand() % 46;
      if (r < 20)
        uri[so_far++] = 'g' + r;
      else
        uri[so_far++] = 'A' + r - 20;
    }
    else {
      uri[so_far++] = data[0];
      data++;
      datalen--;
    }



    r = rand() % 8;

    if (r == 0 && datalen > 0)
      uri[so_far++] = '/';

    if (r == 2 && datalen > 0)
      uri[so_far++] = '_';


    if (so_far > uri_sz - 6) {
      log_warn("too small\n");
      return 0;
    }
  }

  switch(rand()%4){
  case 1:
    memcpy(uri+so_far, ".htm ", 6);
    break;
  case 2:
    memcpy(uri+so_far, ".html ", 7);
    break;
  case 3:
    memcpy(uri+so_far, ".js ", 5);
    break;
  case 0:
    memcpy(uri+so_far, ".swf ", 6);
    break;

  }

  return strlen(uri);

}



int
http_steg_t::http_client_uri_transmit (evbuffer *source, conn_t *conn)
{


  struct evbuffer *dest = conn->outbound();
  struct evbuffer_iovec *iv;
  int i, nv;

  /* Convert all the data in 'source' to hexadecimal and write it to
     'scratch'. Data is padded to a multiple of four characters with
     equals signs. */
  size_t slen = evbuffer_get_length(source);
  size_t datalen = 0;
  int cnt = 0;
  char data[2*slen];

  char outbuf[1024];
  int len =0;
  char buf[10000];

  if (peer_dnsname[0] == '\0')
    lookup_peer_name_from_ip(conn->peername, peer_dnsname);

  nv = evbuffer_peek(source, slen, NULL, NULL, 0);
  iv = (evbuffer_iovec *)xzalloc(sizeof(struct evbuffer_iovec) * nv);
  if (evbuffer_peek(source, slen, NULL, iv, nv) != nv) {
    free(iv);
    return -1;
  }

  for (i = 0; i < nv; i++) {
    const unsigned char *p = (const unsigned char *)iv[i].iov_base;
    const unsigned char *limit = p + iv[i].iov_len;
    char c;
    while (p < limit) {
      c = *p++;
      data[datalen++] = "0123456789abcdef"[(c & 0xF0) >> 4];
      data[datalen++] = "0123456789abcdef"[(c & 0x0F) >> 0];
      }
  }
  free(iv);

  do {
    datalen = gen_uri_field(outbuf, sizeof(outbuf), data, datalen);
  } while (datalen == 0);

  // retry up to 10 times
  while (!len) {
    len = config->payload_server->find_client_payload( buf, sizeof(buf),
                              TYPE_HTTP_REQUEST);
    if (cnt++ == 10) return -1;
  }

  if (evbuffer_add(dest, outbuf, datalen)  ||  // add uri field
      evbuffer_add(dest, "HTTP/1.1\r\nHost: ", 19) ||
      evbuffer_add(dest, peer_dnsname, strlen(peer_dnsname)) ||
      evbuffer_add(dest, strstr(buf, "\r\n"), len - (unsigned int) (strstr(buf, "\r\n") - buf))  ||  // add everything but first line
      evbuffer_add(dest, "\r\n", 2)) {
      log_debug("error ***********************");
      return -1;
  }

  evbuffer_drain(source, slen);
  conn->cease_transmission();
  type = config->payload_server->find_uri_type(outbuf, sizeof(outbuf));
  have_transmitted = 1;
  return 0;

}

int
http_steg_t::transmit(struct evbuffer *source)
{
  //  struct evbuffer *dest = conn_get_outbound(conn);

  //  fprintf(stderr, "in http_ transmit %d\n", downcast_steg(s)->type);



  if (config->is_clientside) {
        /* On the client side, we have to embed the data in a GET query somehow;
	   the only plausible places to put it are the URL and cookies.  */

    /*    if (evbuffer_get_length(source) < 72)
      return http_client_uri_transmit(this, source, conn);
    */

 //@@
    return http_client_cookie_transmit(source, conn); //@@
  }
  else {
    int rval = -1;
    switch(type) {

    //case HTTP_CONTENT_SWF:                    
    //  rval = http_server_SWF_transmit(config->payload_server, source, conn);
     // break;

    case HTTP_CONTENT_JAVASCRIPT:
      rval = http_server_JS_transmit(config->payload_server, source, conn, HTTP_CONTENT_JAVASCRIPT);
      break;

    case HTTP_CONTENT_HTML:
      rval = http_server_JS_transmit(config->payload_server, source, conn, HTTP_CONTENT_HTML);
      break;

    //case HTTP_CONTENT_PDF:
     // rval = http_server_PDF_transmit(config->payload_server, source, conn);
      //break;

    default:
      //this we choose from the steg module array
      rval = config->file_steg_mods[type]->http_server_transmit(source, conn);
      break;
    }

    if (rval >= 0) {
      have_transmitted = 1;
      // FIXME: should decide whether or not to do this based on the
      // Connection: header.  (Needs additional changes elsewhere, esp.
      // in transmit_room.)
      conn->cease_transmission();
    }
    return rval;
  }
}

int
http_steg_t::http_server_receive(conn_t *conn, struct evbuffer *dest, struct evbuffer* source) {

  char* data;
  int type;

  do {
    struct evbuffer_ptr s2 = evbuffer_search(source, "\r\n\r\n", sizeof ("\r\n\r\n") -1 , NULL);
    char *p;
    char *pend;

    char outbuf[MAX_COOKIE_SIZE * 3/2];
    char outbuf2[MAX_COOKIE_SIZE];
    int sofar = 0;
    //int cookie_mode = 0;

    if (s2.pos == -1) {
      log_debug(conn, "Did not find end of request %d",
                (int) evbuffer_get_length(source));
      return RECV_INCOMPLETE;
    }

    log_debug(conn, "SERVER received request header of length %d", (int)s2.pos);

    data = (char*) evbuffer_pullup(source, s2.pos+4);

    if (data == NULL) {
      log_debug(conn, "SERVER evbuffer_pullup fails");
      return RECV_BAD;
    }

    data[s2.pos+3] = 0;

    type = config->payload_server->find_uri_type((char *)data, s2.pos+4);

    if (strstr((char*) data, "Cookie") != NULL) {
      p = strstr((char*) data, "Cookie:") + sizeof "Cookie: "-1;
      //cookie_mode = 1;
    }
    else
      p = data + sizeof "GET /" -1;

    log_debug("Cookie: %s", p);
    pend = strstr(p, "\r\n");
    log_assert(pend);
    if (pend - p > MAX_COOKIE_SIZE * 3/2)
      log_abort(conn, "cookie too big: %lu (max %lu)",
                (unsigned long)(pend - p), (unsigned long)MAX_COOKIE_SIZE);

    memset(outbuf, 0, sizeof(outbuf));
    size_t cookielen = unwrap_b64_cookies(outbuf, p, pend - p);

    base64::decoder D('-', '_', '.');
    memset(outbuf2, 0, sizeof(outbuf2));
    sofar = D.decode(outbuf, cookielen+1, outbuf2);

    if (sofar <= 0)
      log_warn(conn, "base64 decode failed\n");

    if (sofar >= MAX_COOKIE_SIZE)
      log_abort(conn, "cookie decode buffer overflow\n");

    if (evbuffer_add(dest, outbuf2, sofar)) {
      log_debug(conn, "Failed to transfer buffer");
      return RECV_BAD;
    }
    evbuffer_drain(source, s2.pos + sizeof("\r\n\r\n") - 1);
  } while (evbuffer_get_length(source));

  have_received = 1;
  this->type = type;

  // FIXME: should decide whether or not to do this based on the
  // Connection: header. (Needs additional changes elsewhere, esp.
  // in transmit_room.)
  conn->expect_close();

  conn->transmit_soon(WAIT_BEFORE_TRANSMIT);
  return RECV_GOOD;
}

int
http_steg_t::receive(struct evbuffer *dest)
{
  struct evbuffer *source = conn->inbound();

  if (config->is_clientside) {
    return http_client_receive(source, dest);

  } 

  return http_server_receive(conn, dest, source);

}

int
http_steg_t::http_client_receive(evbuffer *source, evbuffer *dest)
{
  int rval = RECV_BAD;
  log_debug(conn, "sur1.5.1");

  switch(type) {
  //case HTTP_CONTENT_SWF:
    //rval = http_handle_client_SWF_receive(this, conn, dest, source);
    //break;

  case HTTP_CONTENT_JAVASCRIPT:
  case HTTP_CONTENT_HTML:
    rval = http_handle_client_JS_receive(this, conn, dest, source);
    break;

  //case HTTP_CONTENT_PDF:
    //rval = http_handle_client_PDF_receive(this, conn, dest, source);
   // break;

  default:
    log_debug(conn, "receiving a payload of type %i", type);
    rval = config->file_steg_mods[type]->http_client_receive(conn, dest, source);
    break;
     
  }

  if (rval == RECV_GOOD) have_received = 1;
  return rval;

}
