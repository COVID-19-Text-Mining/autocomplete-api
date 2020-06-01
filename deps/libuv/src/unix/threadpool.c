/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "internal.h"

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static uv_thread_t threads[4];
static ngx_queue_t exit_message;
static ngx_queue_t wq = { &wq, &wq };
static volatile int initialized;


static void worker(void* arg) {
  struct uv__work* w;
  ngx_queue_t* q;

  (void) arg;

  for (;;) {
    uv_mutex_lock(&mutex);

    while (ngx_queue_empty(&wq))
      uv_cond_wait(&cond, &mutex);

    q = ngx_queue_head(&wq);

    if (q == &exit_message)
      uv_cond_signal(&cond);
    else
      ngx_queue_remove(q);

    uv_mutex_unlock(&mutex);

    if (q == &exit_message)
      break;

    w = ngx_queue_data(q, struct uv__work, wq);
    w->work(w);

    uv_mutex_lock(&w->loop->wq_mutex);
    ngx_queue_insert_tail(&w->loop->wq, &w->wq);
    uv_async_send(&w->loop->wq_async);
    uv_mutex_unlock(&w->loop->wq_mutex);
  }
}


static void post(ngx_queue_t* q) {
  uv_mutex_lock(&mutex);
  ngx_queue_insert_tail(&wq, q);
  uv_cond_signal(&cond);
  uv_mutex_unlock(&mutex);
}


static void init_once(void) {
  unsigned int i;

  ngx_queue_init(&wq);

  for (i = 0; i < ARRAY_SIZE(threads); i++)
    if (uv_thread_create(threads + i, worker, NULL))
      abort();

  initialized = 1;
}


#if defined(__GNUC__)
__attribute__((destructor))
static void cleanup(void) {
  unsigned int i;

  if (initialized == 0)
    return;

  post(&exit_message);

  for (i = 0; i < ARRAY_SIZE(threads); i++)
    if (uv_thread_join(threads + i))
      abort();

  initialized = 0;
}
#endif


void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w)) {
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;
  post(&w->wq);
}


void uv__work_done(uv_async_t* handle, int status) {
  struct uv__work* w;
  uv_loop_t* loop;
  ngx_queue_t* q;
  ngx_queue_t wq;

  loop = container_of(handle, uv_loop_t, wq_async);
  ngx_queue_init(&wq);

  uv_mutex_lock(&loop->wq_mutex);
  if (!ngx_queue_empty(&loop->wq)) {
    q = ngx_queue_head(&loop->wq);
    ngx_queue_split(&loop->wq, q, &wq);
  }
  uv_mutex_unlock(&loop->wq_mutex);

  while (!ngx_queue_empty(&wq)) {
    q = ngx_queue_head(&wq);
    ngx_queue_remove(q);

    w = container_of(q, struct uv__work, wq);
    w->done(w);
  }
}


static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

  req->work_cb(req);
}


static void uv__queue_done(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

  uv__req_unregister(req->loop, req);

  if (req->after_work_cb)
    req->after_work_cb(req);
}


int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  if (work_cb == NULL)
    return uv__set_artificial_error(loop, UV_EINVAL);

  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;
  uv__work_submit(loop, &req->work_req, uv__queue_work, uv__queue_done);
  return 0;
}
