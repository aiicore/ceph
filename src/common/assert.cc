// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2008-2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "BackTrace.h"
#include "common/ceph_context.h"
#include "common/config.h"
#include "common/debug.h"
#include "common/Clock.h"
#include "include/assert.h"
#include "global/signal_handler.h"

#include <errno.h>
#include <iostream>
#include <pthread.h>
#include <sstream>
#include <time.h>
#include <signal.h>

namespace ceph {
  static CephContext *g_assert_context = NULL;

  /* If you register an assert context, assert() will try to lock the dout
   * stream of that context before starting an assert. This is nice because the
   * output looks better. Your assert will not be interleaved with other dout
   * statements.
   *
   * However, this is strictly optional and library code currently does not
   * register an assert context. The extra complexity of supporting this
   * wouldn't really be worth it.
   */
  void register_assert_context(CephContext *cct)
  {
    assert(!g_assert_context);
    g_assert_context = cct;
  }

  static Mutex lock;

  void assert_release_lock() {
    dout_emergency("\n\nReleasing assrt lock.\n\n");
    return;
  }

  void assert_wait_lock() {
    dout_emergency("\n\nSetting assert lock.\n\n");
    return;
  }

  void __ceph_assert_fail(const char *assertion, const char *file, int line,
			  const char *func)
  {
    sig_pthread_info p;
    const sigval sv = { .sival_ptr = (void *) &p };

    p.p_id = pthread_self();
    pthread_getname_np(p.p_id, p.name, sizeof(p.name));

    if (sigqueue(getpid(), SIGUSR1, sv) == 0) {
      // sended ok, so we can wait for responce
      assert_wait_lock();
    }

    /*
    sigset_t newset, oldset;
    sigemptyset(&newset);
    sigaddset(&newset, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &newset, &oldset);

    siginfo_t si;

    char buf[8096] = { 0 };

    dout_emergency("\n\nBEFORE WAIT\n\n");
    sigwaitinfo(&newset, &si);
    dout_emergency("\n\nAFTER WAIT\n\n");

    if(si.si_pid == getpid()) {
      // ok, otherwise back to sigwaitinfo
      dout_emergency("\n\nPID MATCH\n\n");
    }
    */

    ostringstream tss;
    tss << ceph_clock_now(g_assert_context);

    char buf[8096];
    BackTrace *bt = new BackTrace(1);
    snprintf(buf, sizeof(buf),
	     "%s: In function '%s' thread %llx time %s\n"
	     "%s: %d: FAILED assert(%s)\n",
	     file, func, (unsigned long long)pthread_self(), tss.str().c_str(),
	     file, line, assertion);
    dout_emergency(buf);


    // TODO: get rid of this memory allocation.
    ostringstream oss;
    bt->print(oss);
    dout_emergency(oss.str());

    dout_emergency(" NOTE: a copy of the executable, or `objdump -rdS <executable>` "
		   "is needed to interpret this.\n");

    if (g_assert_context) {
      lderr(g_assert_context) << buf << std::endl;
      bt->print(*_dout);
      *_dout << " NOTE: a copy of the executable, or `objdump -rdS <executable>` "
	     << "is needed to interpret this.\n" << dendl;

      g_assert_context->_log->dump_recent();
    }

    abort();
  }

  void __ceph_assertf_fail(const char *assertion, const char *file, int line,
			   const char *func, const char* msg, ...)
  {
    kill(getpid(), SIGUSR1);
    utime_t t(1, 500000000); // 500ms
    t.sleep();

    dout_emergency("HERE, second proc\n");

    ostringstream tss;
    tss << ceph_clock_now(g_assert_context);

    class BufAppender {
    public:
      BufAppender(char* buf, int size) : bufptr(buf), remaining(size) {
      }

      void printf(const char * format, ...) {
	va_list args;
	va_start(args, format);
	this->vprintf(format, args);
	va_end(args);
      }

      void vprintf(const char * format, va_list args) {
	int n = vsnprintf(bufptr, remaining, format, args);
	if (n >= 0) {
	  if (n < remaining) {
	    remaining -= n;
	    bufptr += n;
	  } else {
	    remaining = 0;
	  }
	}
      }

    private:
      char* bufptr;
      int remaining;
    };

    char buf[8096];
    BufAppender ba(buf, sizeof(buf));
    BackTrace *bt = new BackTrace(1);
    ba.printf("%s: In function '%s' thread %llx time %s\n"
	     "%s: %d: FAILED assert(%s)\n",
	     file, func, (unsigned long long)pthread_self(), tss.str().c_str(),
	     file, line, assertion);
    ba.printf("Assertion details: ");
    va_list args;
    va_start(args, msg);
    ba.vprintf(msg, args);
    va_end(args);
    ba.printf("\n");
    dout_emergency(buf);

    dout_emergency("HERE, second proc\n");


    // TODO: get rid of this memory allocation.
    ostringstream oss;
    bt->print(oss);
    dout_emergency(oss.str());

    dout_emergency(" NOTE: a copy of the executable, or `objdump -rdS <executable>` "
		   "is needed to interpret this.\n");

    if (g_assert_context) {
      lderr(g_assert_context) << buf << std::endl;
      bt->print(*_dout);
      *_dout << " NOTE: a copy of the executable, or `objdump -rdS <executable>` "
	     << "is needed to interpret this.\n" << dendl;

      g_assert_context->_log->dump_recent();
    }

    abort();
  }

  void __ceph_assert_warn(const char *assertion, const char *file,
			  int line, const char *func)
  {
    char buf[8096];
    snprintf(buf, sizeof(buf),
	     "WARNING: assert(%s) at: %s: %d: %s()\n",
	     assertion, file, line, func);
    dout_emergency(buf);
  }
}
