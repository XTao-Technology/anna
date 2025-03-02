﻿/*
 Copyright (C) 2021 XTAO Technology <peng.hse@xtaotech.com>.
*/

// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "rgw_rest.h"
#include "rgw_rest_s3.h"
#include "rgw_rest_user.h"
#include "rgw_os_lib.h"
#include "rgw_file.h"
#include "rgw_lib_frontend.h"

namespace rgw {

/* static */
  int RGWHandler_Lib::init_from_header(struct req_state *s)
  {
    string req;
    string first;

    const char *req_name = s->relative_uri.c_str();
    const char *p;

    /* skip request_params parsing, rgw_file should not be
     * seeing any */
    if (*req_name == '?') {
      p = req_name;
    } else {
      p = s->info.request_params.c_str();
    }

    s->info.args.set(p);
    s->info.args.parse();

    if (*req_name != '/')
      return 0;

    req_name++;

    if (!*req_name)
      return 0;

    req = req_name;
    int pos = req.find('/');
    if (pos >= 0) {
      first = req.substr(0, pos);
    } else {
      first = req;
    }

    if (s->bucket_name.empty()) {
      s->bucket_name = std::move(first);
      if (pos >= 0) {
	// XXX ugh, another copy
	string encoded_obj_str = req.substr(pos+1);
	s->object = rgw_obj_key(encoded_obj_str, s->info.args.get("versionId"));
      }
    } else {
      s->object = rgw_obj_key(req_name, s->info.args.get("versionId"));
    }
    return 0;
  } /* init_from_header */

} /* namespace rgw */
