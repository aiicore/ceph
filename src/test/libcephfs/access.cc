// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "gtest/gtest.h"
#include "include/buffer.h"
#include "include/cephfs/libcephfs.h"
#include "include/rados/librados.h"
#include <errno.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/xattr.h>
#include <sys/uio.h>
#include <iostream>
#include <vector>
#include "common/ceph_argparse.h"
#include "json_spirit/json_spirit.h"

#ifdef __linux__
#include <limits.h>
#endif


rados_t cluster;

string key;

int do_mon_command(const char *s, string *key)
{
  char *outs, *outbuf;
  size_t outs_len, outbuf_len;
  int r = rados_mon_command(cluster, (const char **)&s, 1,
			    0, 0,
			    &outbuf, &outbuf_len,
			    &outs, &outs_len);
  if (outbuf_len) {
    string s(outbuf, outbuf_len);
    std::cout << "out: " << s << std::endl;

    // parse out the key
    json_spirit::mValue v, k;
    json_spirit::read_or_throw(s, v);
    k = v.get_array()[0].get_obj().find("key")->second;
    *key = k.get_str();
    std::cout << "key: " << *key << std::endl;
    free(outbuf);
  } else {
    return -EINVAL;
  }
  if (outs_len) {
    string s(outs, outs_len);
    std::cout << "outs: " << s << std::endl;
    free(outs);
  }
  return r;
}

TEST(AccessTest, Foo) {
  // create access key
  string key;
  ASSERT_EQ(0, do_mon_command(
      "{\"prefix\": \"auth get-or-create\", \"entity\": \"client.foo\", "
      "\"caps\": [\"mon\", \"allow *\", \"osd\", \"allow rw\", "
      "\"mds\", \"allow rw\""
      "], \"format\": \"json\"}", &key));

  struct ceph_mount_info *cmount;
  ASSERT_EQ(0, ceph_create(&cmount, "foo"));
  ASSERT_EQ(0, ceph_conf_parse_env(cmount, NULL));
  ASSERT_EQ(0, ceph_conf_read_file(cmount, NULL));
  ASSERT_EQ(0, ceph_conf_set(cmount, "key", key.c_str()));
  ASSERT_EQ(0, ceph_mount(cmount, "/"));

  ceph_shutdown(cmount);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  rados_create(&cluster, NULL);
  rados_conf_read_file(cluster, NULL);
  rados_conf_parse_env(cluster, NULL);
  int r = rados_connect(cluster);
  if (r < 0)
    exit(1);

  r = RUN_ALL_TESTS();

  rados_shutdown(cluster);

  return r;
}