/* Copyright 2011, 2012 SRI International
 * See LICENSE for other credits and copying information
 */

#include "util.h"
#include "../payload_server.h"
 #include "file_steg.h"
#include "jsSteg.h"
//#include "cookies.h"
#include "compression.h"
#include "connections.h"

#include <ctype.h>

#include <event2/buffer.h>

// error codes
#define INVALID_BUF_SIZE	-1
#define INVALID_DATA_CHAR	-2

// controlling content gzipping for jsSteg
#define JS_GZIP_RESP             0

void buf_dump(unsigned char* buf, int len, FILE *out);

ssize_t JSSteg::headless_capacity(char *cover_body, int body_length)
{
  return static_headless_capacity(cover_body,(size_t) body_length);
}


ssize_t JSSteg::capacity(const uint8_t *cover_payload, size_t len)
{
  return static_capacity((char *) cover_payload, (int) len);
}

unsigned int JSSteg::static_capacity(char *cover_payload, int body_length)
{
    ssize_t body_offset = extract_appropriate_respones_body(cover_payload, body_length);
  if (body_offset == -1) {
    return 0; //useless payload
}

  log_debug("at js static headless capacity");
   return static_headless_capacity(cover_payload + body_offset, (size_t) (body_length - body_offset));
}

unsigned int
JSSteg::static_headless_capacity (char* buf, size_t len) {

  return max(0, (static_cast<int>(js_code_block_preliminary_capacity(buf,len)) - JS_DELIMITER_SIZE)/2);
}

unsigned int
JSSteg::js_code_block_preliminary_capacity(char* buf, size_t len) {
  char *bp;
  int cnt=0;
  int j;  

  // jump to the beginning of the body of the HTTP message
  /*hEnd = strstr(buf, "\r\n\r\n");
  if (hEnd == NULL) {
    // cannot find the separator between HTTP header and HTTP body
    return 0;
  }*/
  bp = buf;

  //if (mode == CONTENT_JAVASCRIPT) {
  j = offset2Hex(bp, (buf+len)-bp, 0);
  while (j != -1) {
    cnt++;
    if (j == 0) {
      bp = bp+1;
    } else {
      bp = bp+j+1;
    }

    j = offset2Hex(bp, (buf+len)-bp, 1);
  } // while

  log_debug("code block has capacity %d", cnt);
  return cnt;
}
/*
 * jsSteg: A Javascript-based steganography module
 *
 */


/*
 * int isxString(char *str)
 *
 * description:
 *   return 1 if all char in str are hexadecimal
 *   return 0 otherwise
 *
 */
int isxString(char *str) {
  unsigned int i;
  char *dp = str;
  for (i=0; i<strlen(str); i++) {
    if (! isxdigit(*dp) ) {
      return 0;
    }
  }
  return 1;
}

int JSSteg::isxString(char *str) {
  unsigned int i;
  char *dp = str;
  for (i=0; i<strlen(str); i++) {
    if (! isxdigit(*dp) ) {
      return 0;
    }
  }
  return 1;
}


/*
 * isGzipContent(char *msg)
 *
 * If the HTTP header of msg specifies that the content is gzipped,
 * this function returns 1; otherwise, it returns 0
 *
 * Assumptions:
 * msg is null terminated
 *
 */
int isGzipContent (char *msg) {
  char *ptr = msg, *end;
  int gzipFlag = 0;

  if (!strstr(msg, "\r\n\r\n"))
    return 0;

  while (1) {
    end = strstr(ptr, "\r\n");
    if (end == NULL) {
      break;
    }

    if (!strncmp(ptr, "Content-Encoding: gzip", 22)) {
      gzipFlag = 1;
      break;
    }

    if (!strncmp(end, "\r\n\r\n", 4)){
      break;
    }
    ptr = end+2;
  }

  return gzipFlag;
}


int JSSteg::isGzipContent (char *msg) {
  char *ptr = msg, *end;
  int gzipFlag = 0;

  if (!strstr(msg, "\r\n\r\n"))
    return 0;

  while (1) {
    end = strstr(ptr, "\r\n");
    if (end == NULL) {
      break;
    }

    if (!strncmp(ptr, "Content-Encoding: gzip", 22)) {
      gzipFlag = 1;
      break;
    }

    if (!strncmp(end, "\r\n\r\n", 4)){
      break;
    }
    ptr = end+2;
  }

  return gzipFlag;
}


/*
 * findContentType(char *msg)
 *
 * If the HTTP header of msg specifies that the content type:
 * case (content type)
 *   javascript: return HTTP_CONTENT_JAVASCRIPT
 *   pdf:        return HTTP_CONTENT_PDF
 *   shockwave:  return HTTP_CONTENT_SWF
 *   html:       return HTTP_CONTENT_HTML
 *   otherwise:  return 0
 *
 * Assumptions:
 * msg is null terminated
 *
 */
int findContentType (char *msg) {
  char *ptr = msg, *end;

  if (!strstr(msg, "\r\n\r\n"))
    return 0;

  while (1) {
    end = strstr(ptr, "\r\n");
    if (end == NULL) {
      break;
    }

    if (!strncmp(ptr, "Content-Type:", 13)) {

      if (!strncmp(ptr+14, "text/javascript", 15) ||
          !strncmp(ptr+14, "application/javascript", 22) ||
          !strncmp(ptr+14, "application/x-javascript", 24)) {
        return HTTP_CONTENT_JAVASCRIPT;
      }
      if (!strncmp(ptr+14, "text/html", 9)) {
        return HTTP_CONTENT_HTML;
      }
      if (!strncmp(ptr+14, "application/pdf", 15) ||
          !strncmp(ptr+14, "application/x-pdf", 17)) {
        return HTTP_CONTENT_PDF;
      }
      if (!strncmp(ptr+14, "application/x-shockwave-flash", strlen("application/x-shockwave-flash"))) {
        return HTTP_CONTENT_SWF;
      }
    }

    if (!strncmp(end, "\r\n\r\n", 4)){
      break;
    }
    ptr = end+2;
  }

  return 0;
}

int JSSteg::findContentType (char *msg) {
  char *ptr = msg, *end;

  if (!strstr(msg, "\r\n\r\n"))
    return 0;

  while (1) {
    end = strstr(ptr, "\r\n");
    if (end == NULL) {
      break;
    }

    if (!strncmp(ptr, "Content-Type:", 13)) {

      if (!strncmp(ptr+14, "text/javascript", 15) ||
          !strncmp(ptr+14, "application/javascript", 22) ||
          !strncmp(ptr+14, "application/x-javascript", 24)) {
        return HTTP_CONTENT_JAVASCRIPT;
      }
      if (!strncmp(ptr+14, "text/html", 9)) {
        return HTTP_CONTENT_HTML;
      }
      if (!strncmp(ptr+14, "application/pdf", 15) ||
          !strncmp(ptr+14, "application/x-pdf", 17)) {
        return HTTP_CONTENT_PDF;
      }
      if (!strncmp(ptr+14, "application/x-shockwave-flash", strlen("application/x-shockwave-flash"))) {
        return HTTP_CONTENT_SWF;
      }
    }

    if (!strncmp(end, "\r\n\r\n", 4)){
      break;
    }
    ptr = end+2;
  }

  return 0;
}

/*
 * int encode(char *data, char *jTemplate, char *jData,
 *            unsigned int dlen, unsigned int jtlen, unsigned int jdlen)
 *
 * description:
 *   embed hex-encoded data (data) in the input Javascript (jTemplate)
 *   and put the result in jData
 *   function returns the number of characters in data successfully
 *   embedded in jData, or returns one of the error codes
 *
 * approach:
 *   replaces characters in jTemplate that are hexadecimal (i.e., {0-9,a-f,A-F})
 *   with those in data, and leave the non-hex char in place
 *
 * input:
 *   - data[] : hex data to hide
 *   - dlen   : size of data
 *   - jTemplate[] : Javascript
 *   - jlen   : size of jTemplate
 *   - jdlen  : size of jData, output buffer
 *
 * output:
 *   - jData  : result of encoding data in jTemplate
 *
 * assumptions:
 *   - data is hex-encoded
 *
 * exceptions:
 *   - if (jdlen < jtlen) return INVALID_BUF_SIZE
 *   - if (data contains non-hex char) return INVALID_DATA_CHAR
 *
 * example:
 *   data      = "0123456789ABCDEF"
 *   jTemplate = "dfp_ord=Math.random()*10000000000000000; dfp_tile = 1;"
 *   encode() returns 16
 *   jData     = "01p_or2=M3th.r4n5om()*6789ABCDEF0000000; dfp_tile = 1;"
 *
 */
/*** int encode(char *data, char *jTemplate, char *jData,
           unsigned int dlen, unsigned int jtlen, unsigned int jdlen )
{
  unsigned int encCnt = 0;  
  char *dp, *jtp, *jdp; 

  unsigned int j;

  
  if (jdlen < jtlen) { return INVALID_BUF_SIZE; }

  dp = data; jtp = jTemplate; jdp = jData;

  if (! isxString(dp) ) { return INVALID_DATA_CHAR; }

  
  if (dlen < 1) { return 0; }


  for (j=0; j<jtlen; j++) {
    
    if ( isxdigit(*jtp) ) {
      *jdp = *dp;
      dp++;
      encCnt++;
      if (encCnt == dlen) {
        jtp++; jdp++;
        break;
      }
    } else {
      *jdp = *jtp;
    }
    jtp++; jdp++;
  }


  =
  while (jtp < (jTemplate+jtlen)) {
    *jdp++ = *jtp++;
  }

  return encCnt; 
  }  ***/


#define startScriptTypeJS "<script type=\"text/javascript\">"
#define endScriptTypeJS "</script>"
// #define JS_DELIMITER "?"
// #define JS_DELIMITER_REPLACEMENT "."

ssize_t JSSteg::decode(const uint8_t* cover_payload, size_t cover_len, uint8_t* data) /*char *jData, char *dataBuf, unsigned int jdlen,
             unsigned int dataBufSize, int *fin */
{
  int gzipMode = JS_GZIP_RESP;
  ssize_t decCnt;
  int k, fin;

  if (gzipMode) {
    char buf2[HTTP_PAYLOAD_BUF_SIZE];
    log_debug("gzip content encoding detected");

    ssize_t buf2len = decompress((const uint8_t *)cover_payload, cover_len,
                         (uint8_t *)buf2, HTTP_PAYLOAD_BUF_SIZE);
    if (buf2len <= 0) {
      log_warn("gzInflate for httpBody fails");
      return RECV_BAD;
    }

    buf2[buf2len] = 0;
    cover_payload = (uint8_t*)buf2;
    cover_len = buf2len;

  }

  decCnt = decode_http_body((const char*)cover_payload, (char*)data, cover_len, HTTP_PAYLOAD_BUF_SIZE,
                                  &fin);

  data[decCnt] = 0;

  log_debug("After decodeHTTPBody; decCnt: %ld\n", decCnt);

  // decCnt is an odd number or data is not a hex string
  if (decCnt % 2) {
    log_debug("CLIENT ERROR: An odd number of hex characters received\n");
    return -1;
  }

  if (! isxString((char*)data)) {
    log_debug("CLIENT ERROR: Data received not hex");
    return -1;
  }

  // we are going to decode data in data live!
  // convert hex data back to binary
  char c;
  int i,j;
  for ( i=0, j=0; i< decCnt; i=i+2, ++j) {
    sscanf((char*)&data[i], "%2x", (unsigned int*) &k);
    c = (char)k;
    data[j] = (uint8_t)c;
  }

  return decCnt / 2;

}

/*
 * similar to encode(), but uses offset2Hex to look for usable hex char
 * in JS for encoding. See offset2Hex for what hex char are considered
 * usable. encode() also converts JS_DELIMITER that appears in the
 * the JS to JS_DELIMITER_REPLACEMENT, before all the data is encoded.
 *
 * Output:
 * fin - signal the caller whether all data has been encoded and
 *       a JS_DELIMITER has been added
 */

/**
 *    @param data: the data to be embeded
 *    @param data_len: the length of the data
 *    @param cover_payload: the cover to embed the data into
 *    @param cover_len: cover size in byte
 *
 *    @return < 0 in case of error or length of the cover with embedded dat at success
 *  
 */
int  JSSteg::encode(uint8_t* data, size_t data_len, uint8_t* cover_payload, size_t cover_len) /* char *data,  char *jData,
             unsigned int dlen, unsigned int jtlen,
             unsigned int jdlen, int *fin*/
{
  unsigned int cLen, outbuf2len;  /* num of data encoded in jData */
  uint8_t* outbuf2;

  log_debug("at jssteg encode");
  int gzipMode = JS_GZIP_RESP;
  /*
   *  insanity checks
   */
  //if (jdlen < jtlen) { return INVALID_BUF_SIZE; }
  if (cover_len > SIZE_T_CEILING || data_len > SIZE_T_CEILING ||
      HTTP_PAYLOAD_BUF_SIZE > SIZE_T_CEILING) //remove last condition?
    return -1;

  cLen = headless_capacity((char*)cover_payload, cover_len);
  if (cLen <  data_len) {
    log_warn("not enough cover capacity to embed data");
    return -1; //not enough capacity is an error because you should have check     //before requesting
  }

  size_t hexed_datalen = 2*data_len;
  std::vector<uint8_t> hexed_data(hexed_datalen);

  encode_data_to_hex(data, data_len, hexed_data.data());

  // log_debug("MJS %d %d", datalen, mjs);
  //this should not happen
  log_assert(cover_payload != NULL);

  ssize_t r = encode_http_body((const char*)hexed_data.data(), (char*)cover_payload, (char*)outbuf, hexed_datalen, cover_len, cover_len);

  if (r < 0 || ((unsigned int) r < hexed_datalen)) {
    log_warn("SERVER ERROR: in data encoding");
    return -1;
  }

  // work in progressn
  if (gzipMode == 1) {
    // conservative estimate:
    // sizeof outbuf2 = cLen + 10-byte for gzip header + 8-byte for crc
    outbuf2 = (uint8_t *)xmalloc(cover_len+18); //could be overallocated due to differing size of 18 chars and 18 uint8_ts
    outbuf2len = compress(outbuf, cover_len,
                          outbuf2, cover_len+18, c_format_gzip);

    if (outbuf2len <= 0) {
      log_warn("gzDeflate for outbuf fails");
      free(outbuf2);
      return -1;
    }
    
    memcpy(outbuf,outbuf2, outbuf2len);
    free(outbuf2);
    //free(outbuf);
  } else {
    //outbuf2 = outbuf;
    outbuf2len = cover_len;

  }
  //encCnt isn't really needed any more except for debugging and tracking, but return value outbuf2len is needed for new header
  //return encCnt;
  return outbuf2len;

}

/**
   this function carry the only major part that is different between a
   js file and html file. As such html file will re-implement it accordingly
   As the result encode and decode function for both types remains the same.
*/
int JSSteg::encode_http_body(const char *data, char *jTemplate, char *jData,
                   unsigned int dlen, unsigned int jtlen,
                             unsigned int jdlen)
{
  int fin;
  ssize_t r = encode_in_single_js_block((char*)data, (char*)jTemplate, (char*)jData, dlen, jtlen, jdlen, &fin);

  if (r < 0 || ((unsigned int) r < dlen) || fin == 0) {
    log_warn("SERVER ERROR: Incomplete data encoding");
    return -1;
  }

  return r;

}

/**
   this function carry the only major part of decoding that is different between a
   js file and html file. As such html file will re-implement it accordingly
   As the result encode and decode function for both types remains the same.
*/
int
JSSteg::decode_http_body(const char *jData, const char *dataBuf, unsigned int jdlen,
                       unsigned int dataBufSize, int *fin )
{
  log_assert(dataBufSize >= c_MAX_MSG_BUF_SIZE);
  
  return decode_single_js_block((const char*)jData, (char*)dataBuf, jdlen, HTTP_PAYLOAD_BUF_SIZE,
                                  fin);
}

int  encode_in_single_js_block(char *data, char *jTemplate, char *jData,
             unsigned int dlen, unsigned int jtlen,
             unsigned int jdlen, int *fin)
{
  unsigned int encCnt = 0;  /* num of data encoded in jData */
  char *dp, *jtp, *jdp; /* current pointers for data, jTemplate, and jData */
  int i,j;

  /*
   *  insanity checks
   */
  if (jdlen < jtlen) { return INVALID_BUF_SIZE; }

  dp = data; jtp = jTemplate; jdp = jData;

  if (! isxString(dp) ) { return INVALID_DATA_CHAR; }

  /* handling boundary case: dlen == 0 */
  if (dlen < 1) { return 0; }

  i = offset2Hex(jtp, (jTemplate+jtlen)-jtp, 0);
  while (encCnt < dlen && i != -1) {
    // copy next i char from jtp to jdp,
    // except that if *jtp==JS_DELIMITER, copy
    // JS_DELIMITER_REPLACEMENT to jdp instead
    j = 0;
    while (j < i) {
      if (*jtp == JS_DELIMITER) {
        *jdp = JS_DELIMITER_REPLACEMENT;
      } else {
        *jdp = *jtp;
      }
      jtp = jtp + 1; jdp = jdp + 1; j++;
    }

    *jdp = *dp;
    encCnt++;
    dp = dp + 1; jtp = jtp + 1; jdp = jdp + 1;

    i = offset2Hex(jtp, (jTemplate+jtlen)-jtp, 1);
  }

  // copy the rest of jTemplate to jdata
  // if we've encoded all data, replace the first
  // char in jTemplate by JS_DELIMITER, if needed,
  // to signal the end of data encoding

#ifdef DEBUG2
  printf("encode: encCnt = %d; dlen = %d\n", encCnt, dlen);
#endif

  *fin = 0;
  if (encCnt == dlen) {
    // replace the next char in jTemplate by JS_DELIMITER
    if (jtp < (jTemplate+jtlen)) {
      *jdp = JS_DELIMITER;
    }
    jdp = jdp+1; jtp = jtp+1;
    *fin = 1;
  }

  while (jtp < (jTemplate+jtlen)) {
    if (*jtp == JS_DELIMITER) {
      if (encCnt < dlen) {
        *jdp = JS_DELIMITER_REPLACEMENT;
      } else {
        *jdp = *jtp;
      }
      // else if (isxdigit(*jtp)) {
      //   if (encCnt < dlen && *fin == 0) {
      //     *jdp = JS_DELIMITER;
      //     *fin = 1;
      //   } else {
      //     *jdp = *jtp;
      //   }
      // }
    } else {
      *jdp = *jtp;
    }
    jdp = jdp+1; jtp = jtp+1;
  }

#ifdef DEBUG2
  printf("encode: encCnt = %d; dlen = %d\n", encCnt, dlen);
  printf("encode: fin= %d\n", *fin);
#endif

  return encCnt;

}

/**
  @param data hex data to be encoded in jTemplate
  @param jTemplate the raw javascript payload
  @param jData the buffer which will contain the data encoded into js cover
  
 */
int encodeHTTPBody(char *data, char *jTemplate, char *jData,
                   unsigned int dlen, unsigned int jtlen,
                   unsigned int jdlen, int mode)
{
  char *dp, *jtp, *jdp; // current pointers for data, jTemplate, and jData
  unsigned int encCnt = 0;  // num of data encoded in jData
  int n; // tmp for updating encCnt
  char *jsStart, *jsEnd;
  int skip;
  int scriptLen;
  int fin = 0;
  unsigned int dlen2 = dlen;
  dp = data;
  jtp = jTemplate;
  jdp = jData;


  if (mode == CONTENT_JAVASCRIPT) {
    // assumption: the javascript pertaining to jTemplate has enough capacity
    // to encode jData. thus, we only invoke encode() once here.
    encCnt = encode_in_single_js_block(dp, jtp, jdp, dlen, jtlen, jdlen, &fin);
    // ensure that all dlen char from data have been encoded in jData
#ifdef DEBUG
    if (encCnt != dlen || fin == 0) {
      printf("Problem encoding all data to the JS\n");
    }
#endif
    return encCnt;

  }

  else if (mode == CONTENT_HTML_JAVASCRIPT) {
    while (encCnt < dlen2) {
      jsStart = strstr(jtp, startScriptTypeJS);
      if (jsStart == NULL) {
#ifdef DEBUG
        printf("lack of usable JS; can't find startScriptType\n");
#endif
        return encCnt;
      }
      skip = strlen(startScriptTypeJS)+jsStart-jtp;
#ifdef DEBUG2h
      printf("copying %d (skip) char from jtp to jdp\n", skip);
#endif
      memcpy(jdp, jtp, skip);
      jtp = jtp+skip; jdp = jdp+skip;
      jsEnd = strstr(jtp, endScriptTypeJS);
      if (jsEnd == NULL) {
#ifdef DEBUG
        printf("lack of usable JS; can't find endScriptType\n");
#endif
        return encCnt;
      }

      // the JS for encoding data is between jsStart and jsEnd
      scriptLen = jsEnd - jtp;
      // n = encode2(dp, jtp, jdp, dlen, jtlen, jdlen, &fin);
      n = encode_in_single_js_block(dp, jtp, jdp, dlen, scriptLen, jdlen, &fin);
      // update encCnt, dp, and dlen based on n
      if (n > 0) {
        encCnt = encCnt+n; dp = dp+n; dlen = dlen-n;
      }
      // update jtp, jdp, jdlen
      skip = jsEnd-jtp;
      jtp = jtp+skip; jdp = jdp+skip; jdlen = jdlen-skip;
      skip = strlen(endScriptTypeJS);
      memcpy(jdp, jtp, skip);
      jtp = jtp+skip; jdp = jdp+skip; jdlen = jdlen-skip;
    }

    // copy the rest of jTemplate to jdp
    skip = jTemplate+jtlen-jtp;

    // handling the boundary case in which JS_DELIMITER hasn't been
    // added by encode()
    if (fin == 0 && dlen == 0) {
      if (skip > 0) {
        *jtp = JS_DELIMITER;
        jtp = jtp+1; jdp = jdp+1;
        skip--;
      }
    }
    memcpy(jdp, jtp, skip);
    return encCnt;

  } else {
    log_warn("Unknown mode (%d) for encode()", mode);
    return 0;
  }


}

/*
 * int decode(char *jData, char *dataBuf,
 *            unsigned int jdlen, unsigned int dlen, unsigned int dataBufSize)
 *
 * description:
 *   extract hex char from Javascript embedded with data (jData)
 *   and put the result in dataBuf
 *   function returns the number of hex char extracted from jData
 *   to dataBuf, or returns one of the error codes
 *
 * input:
 *   - jData[]: Javascript embedded with hex-encoded data
 *   - jdlen  : size of jData
 *   - dlen   : size of data to recover
 *   - dataBufSize : size of output data buffer (dataBuf)
 *
 * output:
 *   - dataBuf[] : output buffer for recovered data
 *
 * assumptions:
 *   - data is hex-encoded
 *
 * exceptions:
 *   - if (dlen > dataBufSize) return INVALID_BUF_SIZE
 *
 * example:
 *   jData  = "01p_or2=M3th.r4n5om()*6789ABCDEF0000000; dfp_tile = 1;"
 *   jdlen  = 54
 *   dlen   = 16
 *   dataBufSize = 1000
 *   decode() returns 16
 *   dataBuf= "0123456789ABCDEF"
 *
 */

/***int decode (char *jData, char *dataBuf, unsigned int jdlen,
            unsigned int dlen, unsigned int dataBufSize )
{
  unsigned int decCnt = 0;  
  char *dp, *jdp; 
  unsigned int j;

  if (dlen > dataBufSize) { return INVALID_BUF_SIZE; }

  dp = dataBuf; jdp = jData;
  for (j=0; j<jdlen; j++) {
    if ( isxdigit(*jdp) ) {
      if (decCnt < dlen) {
        decCnt++;
        *dp++ = *jdp++;
      } else {
        break;
      }
    } else {
      jdp++;
    }
  }
  return decCnt;
} ***/


/*
 * decode2() is similar to decode(), but uses offset2Hex to look for
 * applicable hex char in JS for decoding. Also, the decoding process
 * stops when JS_DELIMITER is encountered.
 */
int decode_single_js_block(const char *jData, const char *dataBuf, unsigned int jdlen,
             unsigned int dataBufSize, int *fin )
{
  unsigned int decCnt = 0;  /* num of data decoded */
  char *dp, *jdp; /* current pointers for dataBuf and jData */
  int i,j;
  int cjdlen = jdlen;

  *fin = 0;
  dp = (char*)dataBuf; jdp = (char*)jData;

  i = offset2Hex(jdp, cjdlen, 0);
  while (i != -1) {
    // return if JS_DELIMITER exists between jdp and jdp+i
    for (j=0; j<i; j++) {
      if (*jdp == JS_DELIMITER) {
        *fin = 1;
        return decCnt;
      }
      jdp = jdp+1; cjdlen--;
    }
    // copy hex data from jdp to dp
    if (dataBufSize <= 0) {
      return decCnt;
    }
    *dp = *jdp;
    jdp = jdp+1; cjdlen--;
    dp = dp+1; dataBufSize--;
    decCnt++;

    // find the next hex char
    i = offset2Hex(jdp, cjdlen, 1);
  }

  // look for JS_DELIMITER between jdp to jData+jdlen
  while (jdp < jData+jdlen) {
    if (*jdp == JS_DELIMITER) {
      *fin = 1;
      break;
    }
    jdp = jdp+1;
  }

  return decCnt;
}


int decodeHTTPBody (char *jData, char *dataBuf, unsigned int jdlen,
                    unsigned int dataBufSize, int *fin, int mode )
{
  char *jsStart, *jsEnd;
  char *dp, *jdp; // current pointers for data and jData
  int scriptLen;
  int decCnt = 0;
  int n;
  int dlen = jdlen; //gets rud of unused warning, useless tho
  dp = dataBuf; jdp = jData;

  /*if (mode == CONTENT_JAVASCRIPT) {
    decCnt = decode(jData, dataBuf, jdlen, dataBufSize, fin);
    if (*fin == 0) {
      log_warn("Unable to find JS_DELIMITER");
    }
  }
  else */if (mode == CONTENT_HTML_JAVASCRIPT) {
    *fin = 0;
    dlen = dataBufSize; 
    while (*fin == 0) {
      jsStart = strstr(jdp, startScriptTypeJS);
      if (jsStart == NULL) {
#ifdef DEBUG
        printf("Can't find startScriptType for decoding data inside script type JS\n");
#endif
        return decCnt;
      }
      jdp = jsStart+strlen(startScriptTypeJS);
      jsEnd = strstr(jdp, endScriptTypeJS);
      if (jsEnd == NULL) {
#ifdef DEBUG
        printf("Can't find endScriptType for decoding data inside script type JS\n");
#endif
        return decCnt;
      }

      // the JS for decoding data is between jsStart and jsEnd
      scriptLen = jsEnd - jdp;
      n = decode_single_js_block(jdp, dp, scriptLen, dlen, fin);
      if (n > 0) {
        decCnt = decCnt+n; dlen=dlen-n; dp=dp+n;
      }
      jdp = jsEnd+strlen(endScriptTypeJS);
    } // while (*fin==0)
  } else {
    log_warn("Unknown mode (%d) for decode()", mode);
    return 0;
  }

  return decCnt;
}

void printerr(int err_no) /* name errno had conflict with other vars so I changed it to err_no */
 {
  if (err_no == INVALID_BUF_SIZE) {
    printf ("Error: Output buffer too small\n");
  }
  else if (err_no == INVALID_DATA_CHAR) {
    printf ("Error: Non-hex char in data\n");
  }
  else {
    printf ("Unknown error: %i\n", err_no);
  }
}


/**int testEncode(char *data, char *js, char *outBuf, unsigned int dlen, unsigned int jslen,
               unsigned int outBufLen, int testNum) {
  int r;

  printf ("***** Start of testEncode (%i) *****\n", testNum);
  printf ("Input:\n");
  printf ("data         = %s\n", data);
  printf ("data len     = %i\n", dlen);
  printf ("js           = %s\n", js);
  printf ("js len       = %i\n", jslen);
  r = encode (data, js, outBuf, dlen, jslen, outBufLen);
  if (r < 0) {
    printerr(r); 
  } else {
    printf ("\nOutput:\n");
    printf ("%i char of data embedded in outBuf\n", r);
    outBuf[jslen]    = '\0';
    printf ("outBuf       = %s\n", outBuf);
  }
  printf ("***** End of testEncode (%i) *****\n", testNum);
  return r;
}

int testDecode(char *inBuf, char *outBuf, unsigned int inBufSize, unsigned int dlen,
               unsigned int outBufSize, int testNum) {

  int r;

  printf ("***** Start of testDecode (%i) *****\n", testNum);
  printf ("Input:\n");
  printf ("inBuf       = %s\n", inBuf);
  printf ("inBuf size  = %i\n", inBufSize);
  printf ("data len    = %i\n", dlen);
  printf ("outBuf size = %i\n", outBufSize);
  r = decode(inBuf, outBuf, inBufSize, dlen, outBufSize);
  if (r < 0) {
    printerr(r);
  } else {
    printf ("\nOutput:\n");
    printf ("%i char of data recovered from inBuf (to outBuf)\n", r);
    outBuf[r] = '\0';
    printf ("outBuf   = %s\n", outBuf);
  }
  printf ("***** End of testDecode (%i) *****\n", testNum);
  return r;
}


int testEncode2(char *data, char *js, char *outBuf,
                unsigned int dlen, unsigned int jslen, unsigned int outBufLen,
                int mode, int testNum) {
  int r;
  // int fin;

  printf ("***** Start of testEncode2 (%i) *****\n", testNum);
  printf ("Input:\n");
  printf ("data         = %s\n", data);
  printf ("data len     = %i\n", dlen);
  printf ("js           = %s\n", js);
  printf ("js len       = %i\n", jslen);
  // r = encode2(data, js, outBuf, dlen, jslen, outBufLen, &fin);
  r = encodeHTTPBody(data, js, outBuf, dlen, jslen, outBufLen, mode);

  if (r < 0) {
    printerr(r);
  }
  else {
    printf ("\nOutput:\n");
    printf ("%i char of data embedded in outBuf\n", r);
    //    printf ("fin          = %d\n", fin);
    outBuf[jslen]    = '\0';
    printf ("outBuf       = %s\n", outBuf);

    if ((unsigned int) r < dlen) {
      printf ("Incomplete data encoding\n");
    }
  }
  printf ("***** End of testEncode (%i) *****\n", testNum);
  return r;
}




int testDecode2(char *inBuf, char *outBuf,
             unsigned int inBufSize, unsigned int outBufSize,
             int mode, int testNum) {
  int r;
  int fin;

  printf ("***** Start of testDecode2 (%i) *****\n", testNum);
  printf ("Input:\n");
  printf ("inBuf       = %s\n", inBuf);
  printf ("inBuf size  = %i\n", inBufSize);
  printf ("outBuf size = %i\n", outBufSize);
  r = decodeHTTPBody(inBuf, outBuf, inBufSize, outBufSize, &fin, mode);
  if (r < 0) {
    printerr(r);
  } else {
    printf ("\nOutput:\n");
    printf ("%i char of data recovered from inBuf (to outBuf)\n", r);
    outBuf[r] = '\0';
    printf ("outBuf   = %s\n", outBuf);
  }
  printf ("***** End of testDecode2 (%i) *****\n", testNum);
  return r;
}**/




int
http_server_JS_transmit (PayloadServer* pl, struct evbuffer *source, conn_t *conn,
                         unsigned int content_type)
{
  struct evbuffer_iovec *iv;

  int nv;
  struct evbuffer *dest = conn->outbound();
  size_t sbuflen = evbuffer_get_length(source);
  char *hend, *jsTemplate = NULL, *outbuf, *outbuf2;
  char data[(int) sbuflen*2];
  char newHdr[MAX_RESP_HDR_SIZE];
  unsigned int datalen = 0, cnt = 0, mjs = 0;
  int r, i, mode, jsLen, hLen, cLen, newHdrLen = 0, outbuf2len;

  int gzipMode = JS_GZIP_RESP;

  log_debug("sbuflen = %d\n", (int) sbuflen);

  if (/*content_type != HTTP_CONTENT_JAVASCRIPT &&*/
      content_type != HTTP_CONTENT_HTML) {
    log_warn("SERVER ERROR: Unknown content type (%d)", content_type);
    return -1;
  }

  // log_debug("SERVER: dumping data with length %d:", (int) sbuflen);
  // evbuffer_dump(source, stderr);
  nv = evbuffer_peek(source, sbuflen, NULL, NULL, 0);
  iv = (evbuffer_iovec *)xzalloc(sizeof(struct evbuffer_iovec) * nv);

  if (evbuffer_peek(source, sbuflen, NULL, iv, nv) != nv) {
    free(iv);
    return -1;
  }

  /*if (content_type == HTTP_CONTENT_JAVASCRIPT) {
    mjs = pl->_payload_database.typed_maximum_capacity(HTTP_CONTENT_JAVASCRIPT);
  } else */if (content_type == HTTP_CONTENT_HTML) {
    mjs = pl->_payload_database.typed_maximum_capacity(HTTP_CONTENT_HTML);
  }

  if (mjs <= 0) {
    log_warn("SERVER ERROR: No JavaScript found in jsTemplate");
    return -1;
  }

  if (sbuflen > (size_t) mjs) {
    log_warn("SERVER ERROR: jsTemplate cannot accommodate data %d %dn",
             (int) sbuflen, (int) mjs);
    return -1;
  }

  // Convert data in 'source' to hexadecimal and write it to data
  cnt = 0;
  for (i = 0; i < nv; i++) {
    const unsigned char *p = (const unsigned char *)iv[i].iov_base;
    const unsigned char *limit = p + iv[i].iov_len;
    char c;

    while (p < limit && cnt < sbuflen) {
      c = *p++;
      data[datalen] = "0123456789abcdef"[(c & 0xF0) >> 4];
      data[datalen+1] = "0123456789abcdef"[(c & 0x0F) >> 0];
      datalen += 2;
      cnt++;
    }
  }

  free(iv);

  //log_debug("SERVER encoded data in hex string (len %d):", datalen);
  //    buf_dump((unsigned char*)data, datalen, stderr);

  if (pl->get_payload(content_type, datalen, &jsTemplate, &jsLen) == 1) {
    log_debug("SERVER found the applicable HTTP response template with size %d", jsLen);
  } else {
    log_warn("SERVER couldn't find the applicable HTTP response template");
    return -1;
  }

  // log_debug("MJS %d %d", datalen, mjs);
  if (jsTemplate == NULL) {
    log_warn("NO suitable payload found %d %d", datalen, mjs);
    return -1;
  }

  // assumption: jsTemplate is null-terminated
  hend = strstr(jsTemplate, "\r\n\r\n");
  if (hend == NULL) {
    log_warn("Unable to find end of header in the HTTP template");
    return -1;
  }

  mode = has_eligible_HTTP_content (jsTemplate, jsLen, HTTP_CONTENT_JAVASCRIPT);

  // log_debug("SERVER: using HTTP resp template of length = %d", jsLen);
  // log_debug("HTTP resp tempmlate:");
  // buf_dump((unsigned char*)jsTemplate, jsLen, stderr);

  hLen = hend+4-jsTemplate;
  cLen = jsLen - hLen;
  outbuf = (char *)xmalloc(cLen);

  r = encodeHTTPBody(data, hend+4, outbuf, datalen, cLen, cLen, mode);

  if (r < 0 || ((unsigned int) r < datalen)) {
    log_warn("SERVER ERROR: Incomplete data encoding");
    return -1;
  }

  // work in progress
  if (gzipMode == 1) {
    // conservative estimate:
    // sizeof outbuf2 = cLen + 10-byte for gzip header + 8-byte for crc
    outbuf2 = (char *)xmalloc(cLen+18);

    outbuf2len = compress((const uint8_t *)outbuf, cLen,
                          (uint8_t *)outbuf2, cLen+18, c_format_gzip);

    if (outbuf2len <= 0) {
      log_warn("gzDeflate for outbuf fails");
      free(outbuf2);
      return -1;
    }
    free(outbuf);

  } else {
    outbuf2 = outbuf;
    outbuf2len = cLen;
  }

  // outbuf2 points to the HTTP payload (of length outbuf2len) to be sent

  //if (mode == CONTENT_JAVASCRIPT) { // JavaScript in HTTP body
  //  newHdrLen = gen_response_header((char*) "application/x-javascript", gzipMode,
  //                                  outbuf2len, newHdr, sizeof(newHdr)); }
  if (mode == CONTENT_HTML_JAVASCRIPT) { // JavaScript(s) embedded in HTML doc
    newHdrLen = gen_response_header((char*) "text/html", gzipMode,
                                    outbuf2len, newHdr, sizeof(newHdr));
  } else { // unknown mode
    log_warn("SERVER ERROR: unknown mode for creating the HTTP response header");
    free(outbuf2);
    return -1;
  }
  if (newHdrLen < 0) {
    log_warn("SERVER ERROR: gen_response_header fails for jsSteg");
    free(outbuf2);
    return -1;
  }

  // newHdr points to the HTTP header (of length newHdrLen) to be sent

  if (evbuffer_add(dest, newHdr, newHdrLen)) {
    log_warn("SERVER ERROR: evbuffer_add() fails for newHdr");
    free(outbuf2);
    return -1;
  }

  if (evbuffer_add(dest, outbuf2, outbuf2len)) {
    log_warn("SERVER ERROR: evbuffer_add() fails for outbuf2");
    free(outbuf2);
    return -1;
  }

  evbuffer_drain(source, sbuflen);

  free(outbuf2);
  return 0;
}

int
http_handle_client_JS_receive(steg_t *, conn_t *conn, struct evbuffer *dest, struct evbuffer* source) {
  struct evbuffer_ptr s2;
  int response_len = 0;
  unsigned int content_len = 0;
  unsigned int hdrLen;
  char buf[10];
  char respMsg[HTTP_PAYLOAD_BUF_SIZE];
  char data[c_MAX_MSG_BUF_SIZE];
  char buf2[HTTP_PAYLOAD_BUF_SIZE];

  unsigned char *field, *fieldStart, *fieldEnd, *fieldValStart;
  char *httpBody;

  int decCnt, fin, i, j, k, gzipMode=0, httpBodyLen, buf2len, contentType = 0;
  ev_ssize_t r;
  struct evbuffer * scratch;
  char c;

  s2 = evbuffer_search(source, "\r\n\r\n", sizeof ("\r\n\r\n") -1 , NULL);
  if (s2.pos == -1) {
    log_debug("CLIENT Did not find end of HTTP header %d", (int) evbuffer_get_length(source));
    //      evbuffer_dump(source, stderr);
    return RECV_INCOMPLETE;
    }

  log_debug("CLIENT received response header with len %d", (int)s2.pos);
  char* buf2print = new char[s2.pos+2];
  evbuffer_copyout(source, (void*) buf2print, sizeof(char)* (s2.pos+1));
  buf2print[s2.pos+1] = '\0';
  log_debug("header: %s", buf2print);
  delete[] buf2print;

  response_len = 0;
  hdrLen = s2.pos + strlen("\r\n\r\n");
  response_len += hdrLen;

  // get content length, e.g., Content-Length: 22417
  field = evbuffer_pullup(source, s2.pos);
  if (field == NULL) {
    log_debug("CLIENT unable to pullup the complete HTTP header");
    return RECV_BAD;
  }

  fieldStart = (unsigned char*) strstr((char*) field, "Content-Length: ");
  if (fieldStart == NULL) {
    log_debug("CLIENT unable to find Content-Length in the header");
    return RECV_BAD;
  }

  fieldEnd = (unsigned char*) strstr((char *)fieldStart, "\r\n");
  if (fieldEnd == NULL) {
    log_debug("CLIENT unable to find end of line for Content-Length");
    return RECV_BAD;
  }

  fieldValStart = fieldStart+strlen("Content-Length: ");
  if ((unsigned int) (fieldEnd-fieldValStart) > (sizeof(buf)-1)) {
    log_debug("CLIENT: Value of Content-Length too large");
    return RECV_BAD;
  }
  memcpy(buf, fieldValStart, fieldEnd-fieldValStart);
  buf[fieldEnd-fieldValStart] = 0;

  content_len = atoi(buf);
  log_debug("CLIENT received Content-Length = %d\n", content_len);

  response_len += content_len;

  if (response_len > (int) evbuffer_get_length(source))
    return RECV_INCOMPLETE;

  // read the entire HTTP resp
  if (response_len < HTTP_PAYLOAD_BUF_SIZE) {
    r = evbuffer_copyout(source, respMsg, response_len);
    log_debug("CLIENT %d char copied from source to respMsg (expected %d)", (int)r, response_len);
    if (r < 0) {
      log_debug("CLIENT ERROR: evbuffer_copyout fails");
      return RECV_INCOMPLETE;
    }
    if (r < response_len) {
      log_debug("CLIENT: evbuffer_copyout incomplete; got %d instead of %d", (int)r, response_len);
      return RECV_INCOMPLETE;
    }
    respMsg[response_len] = 0;
  } else {
    log_debug("CLIENT: HTTP response too large to handle");
    return RECV_BAD;
  }

  log_debug("CLIENT received HTTP response with length %d\n", response_len);
  // buf_dump((unsigned char*)respMsg, response_len, stderr);
  // log_debug("HTTP response header:");
  // buf_dump((unsigned char*)respMsg, hdrLen+80, stderr);

  contentType = findContentType (respMsg);
  if (/*contentType != HTTP_CONTENT_JAVASCRIPT && */contentType != HTTP_CONTENT_HTML) {
    log_warn("ERROR: Invalid content type (%d)", contentType);
    return RECV_BAD;
  }

  httpBody = respMsg + hdrLen;
  httpBodyLen = response_len - hdrLen;

  gzipMode = isGzipContent(respMsg);
  if (gzipMode) {
    log_debug("gzip content encoding detected");
    buf2len = decompress((const uint8_t *)httpBody, httpBodyLen,
                         (uint8_t *)buf2, HTTP_PAYLOAD_BUF_SIZE);
    if (buf2len <= 0) {
      log_warn("gzInflate for httpBody fails");
      return RECV_BAD;
    }
    buf2[buf2len] = 0;
    httpBody = buf2;
    httpBodyLen = buf2len;
  }

  /*if (contentType == HTTP_CONTENT_JAVASCRIPT) {
    decCnt = decodeHTTPBody(httpBody, data, httpBodyLen, HTTP_PAYLOAD_BUF_SIZE,
                            &fin, CONTENT_JAVASCRIPT);
  } else {*/
    decCnt = decodeHTTPBody(httpBody, data, httpBodyLen, c_MAX_MSG_BUF_SIZE,
                            &fin, CONTENT_HTML_JAVASCRIPT);
  //}
  data[decCnt] = 0;

  log_debug("After decodeHTTPBody; decCnt: %d\n", decCnt);

  // decCnt is an odd number or data is not a hex string
  if (decCnt % 2) {
    log_debug("CLIENT ERROR: An odd number of hex characters received\n");
    return RECV_BAD;
  }

  if (! isxString(data)) {
    log_debug("CLIENT ERROR: Data received not hex");
    return RECV_BAD;
  }

  // log_debug("Hex data received:");
  //    buf_dump ((unsigned char*)data, decCnt, stderr);

  // get a scratch buffer
  scratch = evbuffer_new();
  if (!scratch) return RECV_BAD;

  if (evbuffer_expand(scratch, decCnt/2)) {
    log_warn("CLIENT ERROR: Evbuffer expand failed \n");
    evbuffer_free(scratch);
    return RECV_BAD;
  }

  // convert hex data back to binary
  for (i=0, j=0; i< decCnt; i=i+2, ++j) {
    sscanf(&data[i], "%2x", (unsigned int*) &k);
    c = (char)k;
    evbuffer_add(scratch, &c, 1);
  }

  // log_debug("CLIENT Done converting hex data to binary:\n");
  // evbuffer_dump(scratch, stderr);


  // add the scratch buffer (which contains the data) to dest

  if (evbuffer_add_buffer(dest, scratch)) {
    evbuffer_free(scratch);
    log_warn("CLIENT ERROR: Failed to transfer buffer");
    return RECV_BAD;
  }
  log_debug("Added scratch (buffer) to dest\n");

  evbuffer_free(scratch);


  if (response_len <= (int) evbuffer_get_length(source)) {
    if (evbuffer_drain(source, response_len) == -1) {
      log_warn("CLIENT ERROR: Failed to drain source");
      return RECV_BAD;
    }
  }
  else {
    log_warn("response_len > buffer size... can't drain");
    exit(-1);
  }


  log_debug("Drained source for %d char\n", response_len);

  //  downcast_steg(s)->have_received = 1;
  conn->expect_close();
  return RECV_GOOD;
}

JSSteg::JSSteg(PayloadServer* payload_provider, double noise2signal, int content_type)
 :FileStegMod(payload_provider, noise2signal, content_type)
{

}

/*****
      int
      main() {
      int jDataSize = 1000;
      char jData[jDataSize];
      int outDataBufSize = 1000;
      char outDataBuf[outDataBufSize];

      int r;
      // test case 1: data embedded in javascript
      r = testEncode2(data1, js1, jData, strlen(data1), strlen(js1), jDataSize,
      CONTENT_JAVASCRIPT, 1);
      if (r > 0) { testDecode2(jData, outDataBuf, strlen(js1), outDataBufSize, CONTENT_JAVASCRIPT, 1); }

      // test case 4: data embedded in one script type javascript
      r = testEncode2(data1, js4, jData, strlen(data1), strlen(js4), jDataSize,
      CONTENT_HTML_JAVASCRIPT, 4);
      if (r > 0) { testDecode2(jData, outDataBuf, strlen(js4), outDataBufSize, CONTENT_HTML_JAVASCRIPT, 4); }

      // test case 5: data embedded in one script type javascript
      r = testEncode2(data1, js5, jData, strlen(data1), strlen(js5), jDataSize,
      CONTENT_HTML_JAVASCRIPT, 5);
      if (r > 0) { testDecode2(jData, outDataBuf, strlen(js5), outDataBufSize, CONTENT_HTML_JAVASCRIPT, 5); }


      return 0;
      }
*****/

/*****
      int
      main() {
      int jDataSize = 1000;
      char jData[jDataSize];
      int jDataSmallSize = 5;
      char jDataSmall[jDataSmallSize];

      int outDataBufSize = 1000;
      char outDataBuf[outDataBufSize];
      int outDataSmallSize = 5;
      char outDataSmall[outDataSmallSize];

      int r;

      // test case 1: data embedded in javascript
      r = testEncode(data1, js1, jData, strlen(data1), strlen(js1), jDataSize, 1);
      if (r > 0) { testDecode(jData, outDataBuf, strlen(js1), r, outDataBufSize, 1); }

      // test case 2: data embedded in javascript
      r = testEncode(data1, js2, jData, strlen(data1), strlen(js2), jDataSize, 2);
      if (r > 0) { testDecode(jData, outDataBuf, strlen(js2), r, outDataBufSize, 2); }

      // test case 3: data partially embedded in javascript; num of hex char in js < data len
      r = testEncode(data1, js3, jData, strlen(data1), strlen(js3), jDataSize, 3);
      if (r > 0) { testDecode(jData, outDataBuf, strlen(js3), r, outDataBufSize, 3); }

      // test case 4: data embedded in javascript; larger data
      r = testEncode(data2, js1, jData, strlen(data2), strlen(js1), jDataSize, 4);
      if (r > 0) { testDecode(jData, outDataBuf, strlen(js1), r, outDataBufSize, 4); }

      // test case 5 (for encode): err for non-hex data
      testEncode(nonhexstr, js1, jData, strlen(nonhexstr), strlen(js1), jDataSize, 5);

      // test case 6 (for encode): err for small output buf
      testEncode(data1, js1, jDataSmall, strlen(data1), strlen(js1), jDataSmallSize, 6);

      // test case 7 (for decode): err for small output buf
      r = testEncode(data1, js1, jData, strlen(data1), strlen(js1), jDataSize, 7);
      if (r > 0) { testDecode(jData, outDataSmall, strlen(js1), r, outDataSmallSize, 7); }
      }
*****/
