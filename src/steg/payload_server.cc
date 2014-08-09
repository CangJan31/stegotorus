/* Copyright 2011, 2012 SRI International
 * Copryight 2012, vmon
 * See LICENSE for other credits and copying information
 */

#include "util.h"
#include "payload_server.h"
#include "file_steg.h"
#include "http_steg_mods/swfSteg.h"
#include "http_steg_mods/pdfSteg.h"
//#include "http_steg_mods/jsSteg.h"
#include <ctype.h>
#include <time.h>


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


  /*if (mode == CONTENT_JAVASCRIPT) {
    // assumption: the javascript pertaining to jTemplate has enough capacity
    // to encode jData. thus, we only invoke encode() once here.
    encCnt = encode(dp, jtp, jdp, dlen, jtlen, jdlen, &fin);
    // ensure that all dlen char from data have been encoded in jData
#ifdef DEBUG
    if (encCnt != dlen || fin == 0) {
      printf("Problem encoding all data to the JS\n");
    }
#endif
    return encCnt;

  }

  else */if (mode == CONTENT_HTML_JAVASCRIPT) {
    while (encCnt < dlen2) {
      jsStart = strstr(jtp, startScriptTypeJS);
      if (jsStart == NULL) {
#ifdef DEBUG
        printf("lack of usable JS; can't find startScriptType\n");
#endif
        return encCnt;
      }
      skip = strlen(startScriptTypeJS)+jsStart-jtp;
#ifdef DEBUG2
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
      n = encode(dp, jtp, jdp, dlen, scriptLen, jdlen, &fin);
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

int decodeHTTPBody (char *jData, char *dataBuf, unsigned int jdlen,
                    unsigned int dataBufSize, int *fin, int mode )
{
  char *jsStart, *jsEnd;
  char *dp, *jdp; // current pointers for data and jData
  int scriptLen;
  int decCnt = 0;
  int n;
  int dlen = dataBufSize;
  dp = dataBuf; jdp = jData;

  /*if (mode == CONTENT_JAVASCRIPT) {
    decCnt = decode(jData, dataBuf, jdlen, dataBufSize, fin);
    if (*fin == 0) {
      log_warn("Unable to find JS_DELIMITER");
    }
  }
  else */if (mode == CONTENT_HTML_JAVASCRIPT) {
    *fin = 0;
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
      n = decode(jdp, dp, scriptLen, dlen, fin);
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

/*
 * capacityJS3 is the next iteration for capacityJS
 */
unsigned int
PayloadServer::capacityJS3 (char* buf, int len, int mode) {
  char *hEnd, *bp, *jsStart, *jsEnd;
  int cnt=0;
  int j;	

  // jump to the beginning of the body of the HTTP message
  hEnd = strstr(buf, "\r\n\r\n");
  if (hEnd == NULL) {
    // cannot find the separator between HTTP header and HTTP body
    return 0;
  }
  bp = hEnd + 4;

  if (mode == CONTENT_JAVASCRIPT) {
    j = offset2Hex(bp, (buf+len)-bp, 0);
    while (j != -1) {
      cnt++;
      if (j == 0) {
        bp = bp+1;
      } else {
        bp = bp+j+1;
      }

      if (len < buf + len - bp) {
	fprintf(stderr, "HERE\n");
      }
      j = offset2Hex(bp, (buf+len)-bp, 1);
    } // while
    return cnt;
  } else if (mode == CONTENT_HTML_JAVASCRIPT) {
     while (bp < (buf+len)) {
       jsStart = strstr(bp, "<script type=\"text/javascript\">");
       if (jsStart == NULL) break;
       bp = jsStart+31;
       jsEnd = strstr(bp, "</script>");
       if (jsEnd == NULL) break;
       // count the number of usable hex char between jsStart+31 and jsEnd

       j = offset2Hex(bp, jsEnd-bp, 0);
       while (j != -1) {
         cnt++;
         if (j == 0) {
           bp = bp+1;
         } else {
           bp = bp+j+1;
         }

         if (len < jsEnd - buf || len < jsEnd - bp) {
           fprintf(stderr, "HERE2\n");
         }
         j = offset2Hex(bp, jsEnd-bp, 1);

       } // while (j != -1)

       if (buf + len < bp + 9) {
         fprintf(stderr, "HERE3\n");
       }


       bp += 9;
     } // while (bp < (buf+len))
     return cnt;
  } else {
    fprintf(stderr, "Unknown mode (%d) for capacityJS() ... \n", mode);
    return 0;
  }
}

unsigned int
PayloadServer::capacityPDF (char* buf, int len) {
  char *hEnd, *bp, *streamStart, *streamEnd;
  int cnt=0;
  int size;

  // jump to the beginning of the body of the HTTP message
  hEnd = strstr(buf, "\r\n\r\n");
  if (hEnd == NULL) {
    // cannot find the separator between HTTP header and HTTP body
    return 0;
  }
  bp = hEnd + 4;

  while (bp < (buf+len)) {
     streamStart = strInBinary("stream", 6, bp, (buf+len)-bp);
     if (streamStart == NULL) break;
     bp = streamStart+6;
     streamEnd = strInBinary("endstream", 9, bp, (buf+len)-bp);
     if (streamEnd == NULL) break;
     // count the number of char between streamStart+6 and streamEnd
     size = streamEnd - (streamStart+6) - 2; // 2 for \r\n before streamEnd
     if (size > 0) {
       cnt = cnt + size;
       //log_debug("capacity of pdf increase by %d", size);
     }
     bp += 9;
  }
  return cnt;
}


/*
 * capacitySWF is just mock function 
  returning the len just for the sake of harmonizing
  the capacity computation. We need to make payload
  types as a children of all classes.
 */
unsigned int 
PayloadServer::capacitySWF(char* buf, int len)
{
  (void)buf;
  return len;
}

/*
 * capacityJS is designed to call capacityJS3 
 */
unsigned int 
PayloadServer::capacityJS (char* buf, int len) {

  int mode = has_eligible_HTTP_content(buf, len, HTTP_CONTENT_JAVASCRIPT);
  if (mode != CONTENT_JAVASCRIPT)
    mode = has_eligible_HTTP_content(buf, len, HTTP_CONTENT_HTML);
  
  if (mode != CONTENT_HTML_JAVASCRIPT && mode != CONTENT_JAVASCRIPT)
    return 0;

  size_t cap = capacityJS3(buf, len, mode);

  if (cap <  JS_DELIMITER_SIZE)
    return 0;
    
  return (cap - JS_DELIMITER_SIZE)/2;
}



/* first line is of the form....
   GET /XX/XXXX.swf[?YYYY] HTTP/1.1\r\n
*/


int 
PayloadServer::find_uri_type(const char* buf_orig, int buflen) {

  char* uri;
  char* ext;

  char* buf = (char *)xmalloc(buflen+1);
  char* uri_end;

  memcpy(buf, buf_orig, buflen);
  buf[buflen] = 0;

  if (strncmp(buf, "GET", 3) != 0
      && strncmp(buf, "POST", 4) != 0) {
    log_debug("Unable to determine URI type. Not a GET/POST requests.\n");
    return -1;
  }

  uri = strchr(buf, ' ') + 1;

  if (uri == NULL) {
    log_debug("Invalid URL\n");
    return -1;
  }

  if (!(uri_end = strchr(uri, '?')))
    uri_end = strchr(uri, ' ');
  

  if (uri_end == NULL) {
    log_debug("unterminated uri\n");
    return -1;
  }

  uri_end[0] = 0;

  ext = strrchr(uri, '/');

  if (ext == NULL) {
    log_debug("no / in url: find_uri_type...");
    return -1;
  }

  ext = strchr(ext, '.');

  if (ext == NULL || !strncmp(ext, ".html", 5) || !strncmp(ext, ".htm", 4) || !strncmp(ext, ".php", 4)
      || !strncmp(ext, ".jsp", 4) || !strncmp(ext, ".asp", 4))
    return HTTP_CONTENT_HTML;

  if (!strncmp(ext, ".js", 3) || !strncmp(ext, ".JS", 3))
    return HTTP_CONTENT_JAVASCRIPT;

  if (!strncmp(ext, ".pdf", 4) || !strncmp(ext, ".PDF", 4))
    return HTTP_CONTENT_PDF;


  if (!strncmp(ext, ".swf", 4) || !strncmp(ext, ".SWF", 4))
    return HTTP_CONTENT_SWF;

  if (!strncmp(ext, ".png", 4) || !strncmp(ext, ".PNG", 4))
    return HTTP_CONTENT_PNG;

  if (!strncmp(ext, ".jpg", 4) || !strncmp(ext, ".JPG", 4))
    return HTTP_CONTENT_JPEG;

  if (!strncmp(ext, ".gif", 4) || !strncmp(ext, ".GIF", 4))
    return HTTP_CONTENT_GIF;

  free(buf);
  return -1;
  
}


/*
 * fixContentLen corrects the Content-Length for an HTTP msg that
 * has been ungzipped, and removes the "Content-Encoding: gzip"
 * field from the header.
 *
 * The function returns -1 if no change to the HTTP msg has been made,
 * when the msg wasn't gzipped or an error has been encountered
 * If fixContentLen changes the msg header, it will put the new HTTP
 * msg in buf and returns the length of the new msg
 *
 * Input:
 * payload - pointer to the (input) HTTP msg
 * payloadLen - length of the (input) HTTP msg
 *
 * Ouptut:
 * buf - pointer to the buffer containing the new HTTP msg
 * bufLen - length of buf
 * 
 */
int
fixContentLen (char* payload, int payloadLen, char *buf, int bufLen) {

  int gzipFlag=0, clFlag=0, clZeroFlag=0;
  char* ptr = payload;
  char* clPtr = payload;
  char* gzipPtr = payload;
  char* end;


  char *cp, *clEndPtr;
  int hdrLen, bodyLen, r, len;





  // note that the ordering between the Content-Length and the Content-Encoding
  // in an HTTP msg may be different for different msg 

  // if payloadLen is larger than the size of our buffer,
  // stop and return -1
  if (payloadLen > bufLen) { return -1; }

  while (1) {
    end = strstr(ptr, "\r\n");
    if (end == NULL) {
      // log_debug("invalid header %d %d %s \n", payloadLen, (int) (ptr - payload), payload);
      return -1;
    }

    if (!strncmp(ptr, "Content-Encoding: gzip\r\n", 24)) {
        gzipFlag = 1;
        gzipPtr = ptr;     
    } else if (!strncmp(ptr, "Content-Length: 0", 17)) {
        clZeroFlag = 1;
    } else if (!strncmp(ptr, "Content-Length:", 15)) {
        clFlag = 1;
        clPtr = ptr;
    }

    if (!strncmp(end, "\r\n\r\n", 4)){
      break;
    }
    ptr = end+2;
  }

  // stop if zero Content-Length or Content-Length not found
  if (clZeroFlag || ! clFlag) return -1;
  
  // end now points to the end of the header, before "\r\n\r\n"
  cp=buf;
  bodyLen = (int)(payloadLen - (end+4-payload));

  clEndPtr = strstr(clPtr, "\r\n");
  if (clEndPtr == NULL) {
    log_debug("unable to find end of line for Content-Length");
    return -1;
  }
  if (gzipFlag && clFlag) {
    if (gzipPtr < clPtr) { // Content-Encoding appears before Content-Length

      // copy the part of the header before Content-Encoding
      len = (int)(gzipPtr-payload);
      memcpy(cp, payload, len);
      cp = cp+len;

      // copy the part of the header between Content-Encoding and Content-Length
      // skip 24 char, the len of "Content-Encoding: gzip\r\n"
      // *** this is temporary; we'll remove this after the obfsproxy can perform gzip
      len = (int)(clPtr-(gzipPtr+24));  
      memcpy(cp, gzipPtr+24, len);
      cp = cp+len;

      // put the new Content-Length
      memcpy(cp, "Content-Length: ", 16);
      cp = cp+16;
      r = sprintf(cp, "%d\r\n", bodyLen);
      if (r < 0) {
        log_debug("sprintf fails");
        return -1;
      }
      cp = cp+r;

      // copy the part of the header after Content-Length, if any
      if (clEndPtr != end) { // there is header info after Content-Length
        len = (int)(end-(clEndPtr+2));
        memcpy(cp, clEndPtr+2, len);
        cp = cp+len;
        memcpy(cp, "\r\n\r\n", 4);
        cp = cp+4;
      } else { // Content-Length is the last hdr field
        memcpy(cp, "\r\n", 2);
        cp = cp+2;
      }

      hdrLen = cp-buf;

/****
log_debug("orig: hdrLen = %d, bodyLen = %d, payloadLen = %d", (int)(end+4-payload), bodyLen, payloadLen);
log_debug("new: hdrLen = %d, bodyLen = %d, payloadLen = %d", hdrLen, bodyLen, hdrLen+bodyLen);
 ****/

      // copy the HTTP body
      memcpy(cp, end+4, bodyLen);
      return (hdrLen+bodyLen);

    } else { // Content-Length before Content-Encoding
      // copy the part of the header before Content-Length
      len = (int)(clPtr-payload);
      memcpy(cp, payload, len);
      cp = cp+len;

      // put the new Content-Length
      memcpy(cp, "Content-Length: ", 16);
      cp = cp+16;
      r = sprintf(cp, "%d\r\n", bodyLen);
      if (r < 0) {
        log_debug("sprintf fails");
        return -1;
      }
      cp = cp+r;

      // copy the part of the header between Content-Length and Content-Encoding
      len = (int)(gzipPtr-(clEndPtr+2));
      memcpy(cp, clEndPtr+2, len);
      cp = cp+len;
      
      // copy the part of the header after Content-Encoding
      // skip 24 char, the len of "Content-Encoding: gzip\r\n"
      // *** this is temporary; we'll remove this after the obfsproxy can perform gzip
      if (end > (gzipPtr+24)) { // there is header info after Content-Encoding
        len = (int)(end-(gzipPtr+24));
        memcpy(cp, gzipPtr+24, len);
        cp = cp+len;
        memcpy(cp, "\r\n\r\n", 4);
        cp = cp+4;
      } else { // Content-Encoding is the last field in the hdr
        memcpy(cp, "\r\n", 2);
        cp = cp+2;
      }
      hdrLen = cp-buf;

/****
log_debug("orig: hdrLen = %d, bodyLen = %d, payloadLen = %d", (int)(end+4-payload), bodyLen, payloadLen);
log_debug("new: hdrLen = %d, bodyLen = %d, payloadLen = %d", hdrLen, bodyLen, hdrLen+bodyLen);
 ****/

      // copy the HTTP body
      memcpy(cp, end+4, bodyLen);
      return (hdrLen+bodyLen);
    }
  }
  return -1;
}

void
gen_rfc_1123_date(char* buf, int buf_size) {
  time_t t = time(NULL);
  struct tm *my_tm = gmtime(&t);
  strftime(buf, buf_size, "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", my_tm);
}

void
gen_rfc_1123_expiry_date(char* buf, int buf_size) {
  time_t t = time(NULL) + rand() % 10000;
  struct tm *my_tm = gmtime(&t);
  strftime(buf, buf_size, "Expires: %a, %d %b %Y %H:%M:%S GMT\r\n", my_tm);
}

int
gen_response_header(char* content_type, int gzip, int length, char* buf, int buflen) {
  char* ptr;

  // conservative assumption here.... 
  if (buflen < 400) {
    fprintf(stderr, "gen_response_header: buflen too small\n");
    return -1;
  }

  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  ptr = buf + strlen("HTTP/1.1 200 OK\r\n");
  gen_rfc_1123_date(ptr, buflen - (ptr - buf));
  ptr = ptr + strlen(ptr);

  sprintf(ptr, "Server: Apache\r\n");
  ptr = ptr + strlen(ptr);

  switch(rand() % 9) {
  case 1:
    sprintf(ptr, "Vary: Cookie\r\n");
    ptr = ptr + strlen(ptr);
    break;

  case 2:
    sprintf(ptr, "Vary: Accept-Encoding, User-Agent\r\n");
    ptr = ptr + strlen(ptr);
    break;

  case 3:
    sprintf(ptr, "Vary: *\r\n");
    ptr = ptr + strlen(ptr);
    break;

  }


  switch(rand() % 4) {
  case 2:
    gen_rfc_1123_expiry_date(ptr, buflen - (ptr - buf));
    ptr = ptr + strlen(ptr);
  }
 

  if (gzip) 
    sprintf(ptr, "Content-Length: %d\r\nContent-Encoding: gzip\r\nContent-Type: %s\r\n", length, content_type);
  else
    sprintf(ptr, "Content-Length: %d\r\nContent-Type: %s\r\n", length, content_type);
    
  ptr += strlen(ptr);

  switch(rand() % 4) {
  case 2:
  case 3:
  case 4:
    sprintf(ptr, "Connection: Keep-Alive\r\n\r\n");
    break;
  default:
    sprintf(ptr, "Connection: close\r\n\r\n");
    break;    
  }

  ptr += strlen(ptr);

  return ptr - buf;
}






int
parse_client_headers(char* inbuf, char* outbuf, int len) {
  // client-side
  // remove Host: field
  // remove referrer fields?

  char* ptr = inbuf;
  int outlen = 0;

  while (1) {
    // char* end = strstr(ptr, "\r\n", len - (ptr - inbuf));
    char* end = strstr(ptr, "\r\n");
    if (end == NULL) {
      fprintf(stderr, "invalid client header %d %d %s \n PTR = %s\n", len, (int) (len - (ptr - inbuf)), inbuf, ptr);
      // fprintf(stderr, "HERE %s\n", ptr);
      break;
    }

    if (!strncmp(ptr, "Host:", 5) ||
	!strncmp(ptr, "Referer:", 8) ||
	!strncmp(ptr, "Cookie:", 7)) {
      goto next;
    }

    memcpy(outbuf + outlen, ptr, end - ptr + 2);
    outlen += end - ptr + 2;

  next:
    if (!strncmp(end, "\r\n\r\n", 4)){
      break;
    }
    ptr = end+2;
  }
  
  return outlen;

  // server-side
  // fix date fields
  // fix content-length



}




/*
 * skipJSPattern returns the number of characters to skip when
 * the input pointer matches the start of a common JavaScript
 * keyword 
 *
 * todo: 
 * Use a more efficient regular expression matching algo
 */



int
skipJSPattern(char *cp, int len) {
  int i,j;


  char keywords [21][10]= {"function", "return", "var", "int", "random", "Math", "while",
			   "else", "for", "document", "write", "writeln", "true",
			   "false", "True", "False", "window", "indexOf", "navigator", "case", "if"};


  if (len < 1) return 0;

  // change the limit to 21 to enable if as a keyword
  for (i=0; i < 20; i++) {
    char* word = keywords[i];
    
    if (len <= (int) strlen(word))
      continue;

    if (word[0] != cp[0])
      continue;

    for (j=1; j < (int) strlen(word); j++) {
      if (isxdigit(word[j])) {
	if (!isxdigit(cp[j]))
	  goto next_word;
	else
	  continue;
      }
      
      if (cp[j] != word[j])
	goto next_word;
    }
    if (!isalnum(cp[j]) && cp[j] != JS_DELIMITER && cp[j] != JS_DELIMITER_REPLACEMENT)
      return strlen(word)+1;
      
  next_word:
    continue;
  }

  return 0;
}







int
isalnum_ (char c) {
  if (isalnum(c) || c == '_') return 1;
  else return 0;
}

int
offset2Alnum_ (char *p, int range) {
  char *cp = p;

  while ((cp < (p+range)) && !isalnum_(*cp)) {
    cp++;
  }

  if (cp < (p+range)) {
    return (cp-p);
  } else {
    return -1;
  }
}



/*
 * offset2Hex returns the offset to the next usable hex char.
 * usable here refer to char that our steg module can use to encode
 * data. in particular, words that correspond to common JavaScript keywords
 * are not used for data encoding (see skipJSPattern). Also, because
 * JS var name must start with an underscore or a letter (but not a digit)
 * we don't use the first char of a word for encoding data
 *
 * e.g., the JS statement "var a;" won't be used for encoding data
 * because "var" is a common JS keyword and "a" is the first char of a word
 *
 * Input:
 * p - ptr to the starting pos 
 * range - max number of char to look
 * isLastCharHex - is the char pointed to by (p-1) a hex char 
 *
 * Output:
 * offset2Hex returns the offset to the next usable hex char
 * between p and (p+range), if it exists;
 * otherwise, it returns -1
 *
 */
int
offset2Hex (char *p, int range, int isLastCharHex) {
  char *cp = p;
  int i,j;
  int isFirstWordChar = 1;

  if (range < 1) return -1;

  // case 1: last char is hexadecimal
  if (isLastCharHex) {
    if (isxdigit(*cp)) return 0; // base case
    else {
      while (cp < (p+range) && isalnum_(*cp)) {
        cp++;
        if (isxdigit(*cp)) return (cp-p);
      }
      if (cp >= (p+range)) return -1;
      // non-alnum_ found
      // fallthru and handle case 2
    }
  }
 
  // case 2:
  // find the next word that starts with alnum or underscore,
  // which could be a variable, keyword, or literal inside a string

  i = offset2Alnum_(cp, p+range-cp);
  if (i == -1) return -1;

  while (cp < (p+range) && i != -1) {

    if (i == 0) { 
      if (isFirstWordChar) {
        j = skipJSPattern(cp, p+range-cp); 
        if (j > 0) {
          cp = cp+j;
        } else {
          cp++; isFirstWordChar = 0; // skip the 1st char of a word
        }
      } else { // we are in the middle of a word; no need to invoke skipJSPattern
        if (isxdigit(*cp)) return (cp-p);
        if (! isalnum_(*cp)) {
          isFirstWordChar = 1;
        }
        cp++;
     }
   } else {
     cp += i; isFirstWordChar = 1;
   }
   i = offset2Alnum_(cp, p+range-cp);

  } // while

  // cannot find next usable hex char 
  return -1;
 
}


/*
 * strInBinary looks for char array pattern of length patternLen in a char array
 * blob of length blobLen
 *
 * return a pointer for the first occurrence of pattern in blob, if found
 * otherwise, return NULL
 * 
 */
char *
strInBinary (const char *pattern, unsigned int patternLen, 
             const char *blob, unsigned int blobLen) {
  int found = 0;
  char *cp = (char *)blob;

  while (1) {
    if (blob+blobLen-cp < (int) patternLen) break;
    if (*cp == pattern[0]) {
      if (memcmp(cp, pattern, patternLen) == 0) {
        found = 1;
        break;
      }
    }
    cp++; 
  }
  if (found) return cp;
  else return NULL;
}


/*
 * has_eligible_HTTP_content() identifies if the input HTTP message 
 * contains a specified type of content, used by a steg module to
 * select candidate HTTP message as cover traffic
 */

// for JavaScript, there are two cases:
// 1) If Content-Type: has one of the following values
//       text/javascript 
//       application/x-javascript
//       application/javascript
// 2) Content-Type: text/html and 
//    HTTP body contains <script type="text/javascript"> ... </script>
// #define CONTENT_JAVASCRIPT		1 (for case 1)
// #define CONTENT_HTML_JAVASCRIPT	2 (for case 2)
//
// for pdf, we look for the msgs whose Content-Type: has one of the
// following values
// 1) application/pdf
// 2) application/x-pdf
// 

int 
has_eligible_HTTP_content (char* buf, int len, int type) {
  char* ptr = buf;
  char* matchptr;
  int tjFlag=0, thFlag=0, ceFlag=0, teFlag=0, http304Flag=0, clZeroFlag=0, pdfFlag=0, swfFlag=0; //, gzipFlag=0; // compiler under Ubuntu complains about unused vars, so commenting out until we need it
  char* end, *cp;

#ifdef DEBUG
  fprintf(stderr, "TESTING availabilty of js in payload ... \n");
#endif

  if (type != HTTP_CONTENT_JAVASCRIPT &&
      type != HTTP_CONTENT_HTML &&
      type != HTTP_CONTENT_PDF && type != HTTP_CONTENT_SWF)
    return 0;

  // assumption: buf is null-terminated
  if (!strstr(buf, "\r\n\r\n"))
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
	tjFlag = 1;
      }
      if (!strncmp(ptr+14, "text/html", 9)) {
	thFlag = 1;
      }
      if (!strncmp(ptr+14, "application/pdf", 15) || 
	  !strncmp(ptr+14, "application/x-pdf", 17)) {
	pdfFlag = 1;
      }
      if (!strncmp(ptr+14, "application/x-shockwave-flash", strlen("application/x-shockwave-flash"))) {
	swfFlag = 1;
      }

    } else if (!strncmp(ptr, "Content-Encoding: gzip", 22)) {
      //      gzipFlag = 1; // commented out as variable is set but never read and Ubuntu compiler complains
    } else if (!strncmp(ptr, "Content-Encoding:", 17)) { // Content-Encoding that is not gzip
      ceFlag = 1;
    } else if (!strncmp(ptr, "Transfer-Encoding:", 18)) {
      teFlag = 1;
    } else if (!strncmp(ptr, "HTTP/1.1 304 ", 13)) {
      http304Flag = 1;
    } else if (!strncmp(ptr, "Content-Length: 0", 17)) {
      clZeroFlag = 1;
    }
    
    if (!strncmp(end, "\r\n\r\n", 4)){
      break;
    }
    ptr = end+2;
  }

#ifdef DEBUG
  printf("tjFlag=%d; thFlag=%d; gzipFlag=%d; ceFlag=%d; teFlag=%d; http304Flag=%d; clZeroFlag=%d\n", 
    tjFlag, thFlag, gzipFlag, ceFlag, teFlag, http304Flag, clZeroFlag);
#endif

  // if (type == HTTP_CONTENT_JAVASCRIPT)
  if (type == HTTP_CONTENT_JAVASCRIPT || type == HTTP_CONTENT_HTML) {
    // empty body if it's HTTP not modified (304) or zero Content-Length
    if (http304Flag || clZeroFlag) return 0; 

    // for now, we're not dealing with Transfer-Encoding (e.g., chunked)
    // or Content-Encoding that is not gzip
    // if (teFlag) return 0;
    if (teFlag || ceFlag) return 0;

    if (tjFlag && ceFlag && end != NULL) {
      log_debug("(JS) gzip flag detected with hdr len %d", (int)(end-buf+4));
    } else if (thFlag && ceFlag && end != NULL) {
      log_debug("(HTML) gzip flag detected with hdr len %d", (int)(end-buf+4));
    }

    // case 1
    if (tjFlag) return 1; 

    // case 2: check if HTTP body contains <script type="text/javascript">
    if (thFlag) {
      matchptr = strstr(ptr, "<script type=\"text/javascript\">");
      if (matchptr != NULL) {
        return 2;
      }
    }
  }

  if (type == HTTP_CONTENT_PDF && pdfFlag) {
    // reject msg with empty body: HTTP not modified (304) or zero Content-Length
    if (http304Flag || clZeroFlag) return 0; 

    // for now, we're not dealing with Transfer-Encoding (e.g., chunked)
    // or Content-Encoding that is not gzip
    // if (teFlag) return 0;
    if (teFlag || ceFlag) return 0;

    // check if HTTP body contains "endstream";
    // strlen("endstream") == 9
    
    cp = strInBinary("endstream", 9, ptr, buf+len-ptr);
    if (cp != NULL) {
      // log_debug("Matched endstream!");
      return 1;
    }
  }
  
  //check if we need to update this for current SWF implementation
  if (type == HTTP_CONTENT_SWF && swfFlag == 1 && 
      ((len + buf - end) > SWF_SAVE_FOOTER_LEN + SWF_SAVE_HEADER_LEN + 8))
    return 1;

  return 0;
}



int
find_content_length (char *hdr, int /*hlen*/) {
  char *clStart;
  char* clEnd;
  char *clValStart;
  int valLen;
  int contentLen;
  char buf[10];

  clStart = strstr(hdr, "Content-Length: ");
  if (clStart == NULL) {
    log_debug("Unable to find Content-Length in the header");
    return -1;
  }

  clEnd = strstr((char *)clStart, "\r\n");
  if (clEnd == NULL) {
    log_debug("Unable to find end of line for Content-Length");
    return -1;
  }

  // clValStart = clStart+strlen("Content-Length: ");
  clValStart = clStart+16;

  valLen = clEnd-clValStart;
  if (valLen > 9) return -1;
  memcpy(buf, clValStart, valLen);
  buf[valLen] = 0;
  contentLen = atoi(buf);
  return contentLen;
}

