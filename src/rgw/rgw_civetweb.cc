﻿/*
 Copyright (C) 2021 XTAO Technology <peng.hse@xtaotech.com>.
*/

// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <string.h>

#include "civetweb/civetweb.h"
#include "rgw_civetweb.h"


#define dout_subsys ceph_subsys_rgw

int RGWMongoose::write_data(const char *buf, int len)
{
  if (!header_done) {
    header_data.append(buf, len);
    return len;
  }
  if (!sent_header) {
    data.append(buf, len);
    return len;
  }
  int r = mg_write(conn, buf, len);
  if (r == 0) {
    /* didn't send anything, error out */
    return -EIO;
  }
  return r;
}

RGWMongoose::RGWMongoose(mg_connection *_conn, int _port)
  : conn(_conn), port(_port), status_num(0), header_done(false),
    sent_header(false), has_content_length(false),
    explicit_keepalive(false), explicit_conn_close(false)
{
}

int RGWMongoose::read_data(char *buf, int len)
{
  return mg_read(conn, buf, len);
}

void RGWMongoose::flush()
{
}

int RGWMongoose::complete_request()
{
  if (!sent_header) {
    if (!has_content_length) {

      header_done = false; /* let's go back to writing the header */

      /*
       * Status 204 should not include a content-length header
       * RFC7230 says so
       *
       * Same goes for status 304: Not Modified
       *
       * 'If a cache uses a received 304 response to update a cache entry,'
       * 'the cache MUST update the entry to reflect any new field values'
       * 'given in the response.'
       *
       */
      if (status_num == 204 || status_num == 304) {
        has_content_length = true;
      } else if (0 && data.length() == 0) {
        has_content_length = true;
        print("Transfer-Enconding: %s\r\n", "chunked");
        data.append("0\r\n\r\n", sizeof("0\r\n\r\n")-1);
      } else {
	int r = send_content_length(data.length());
	if (r < 0)
	  return r;
      }
    }

    complete_header();
  }

  if (data.length()) {
    int r = write_data(data.c_str(), data.length());
    if (r < 0)
      return r;
    data.clear();
  }

  return 0;
}

void RGWMongoose::init_env(CephContext *cct)
{
  env.init(cct);
  struct mg_request_info *info = mg_get_request_info(conn);

  if (!info)
    return;

  for (int i = 0; i < info->num_headers; i++) {
    struct mg_request_info::mg_header *header = &info->http_headers[i];

    if (strcasecmp(header->name, "content-length") == 0) {
      env.set("CONTENT_LENGTH", header->value);
      continue;
    }

    if (strcasecmp(header->name, "content-type") == 0) {
      env.set("CONTENT_TYPE", header->value);
      continue;
    }

    if (strcasecmp(header->name, "connection") == 0) {
      explicit_keepalive = (strcasecmp(header->value, "keep-alive") == 0);
      explicit_conn_close = (strcasecmp(header->value, "close") == 0);
    }

    int len = strlen(header->name) + 5; /* HTTP_ prepended */
    char buf[len + 1];
    memcpy(buf, "HTTP_", 5);
    const char *src = header->name;
    char *dest = &buf[5];
    for (; *src; src++, dest++) {
      char c = *src;
      switch (c) {
       case '-':
	 c = '_';
	 break;
      default:
	c = toupper(c);
	break;
      }
      *dest = c;
    }
    *dest = '\0';

    env.set(buf, header->value);
  }

  env.set("REQUEST_METHOD", info->request_method);
  env.set("REQUEST_URI", info->uri);
  env.set("QUERY_STRING", info->query_string);
  env.set("REMOTE_USER", info->remote_user);
  env.set("SCRIPT_URI", info->uri); /* FIXME */

  char port_buf[16];
  snprintf(port_buf, sizeof(port_buf), "%d", port);
  env.set("SERVER_PORT", port_buf);

  if (info->is_ssl) {
    if (port == 0) {
      strcpy(port_buf,"443");
    }
    env.set("SERVER_PORT_SECURE", port_buf);
  }
}

int RGWMongoose::send_status(int status, const char *status_name)
{
  char buf[128];

  if (!status_name)
    status_name = "";

  snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", status, status_name);

  bufferlist bl;
  bl.append(buf);
  bl.append(header_data);
  header_data = bl;

  status_num = status;
  mg_set_http_status(conn, status_num);

  return 0;
}

int RGWMongoose::send_100_continue()
{
  char buf[] = "HTTP/1.1 100 CONTINUE\r\n\r\n";

  return mg_write(conn, buf, sizeof(buf) - 1);
}

static void dump_date_header(bufferlist &out)
{
  char timestr[TIME_BUF_SIZE];
  const time_t gtime = time(NULL);
  struct tm result;
  struct tm const * const tmp = gmtime_r(&gtime, &result);

  if (tmp == NULL)
    return;

  if (strftime(timestr, sizeof(timestr),
	       "Date: %a, %d %b %Y %H:%M:%S %Z\r\n", tmp))
    out.append(timestr);
}

int RGWMongoose::complete_header()
{
  header_done = true;

  if (!has_content_length) {
    return 0;
  }

  dump_date_header(header_data);

  if (explicit_keepalive)
    header_data.append("Connection: Keep-Alive\r\n");
  else if (explicit_conn_close)
    header_data.append("Connection: close\r\n");

  header_data.append("\r\n");

  sent_header = true;

  return write_data(header_data.c_str(), header_data.length());
}

int RGWMongoose::send_content_length(uint64_t len)
{
  has_content_length = true;
  char buf[21];
  snprintf(buf, sizeof(buf), "%" PRIu64, len);
  return print("Content-Length: %s\r\n", buf);
}
