/*
 * Copyright (c) 2021 Cynthia K. Rey, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Replacement for libgit2 http transport, that uses js bindings to node' http api

#include <emscripten.h>
#include <git2/transport.h>
#include "smart.h"
#include "http.h"

// Constants -- copied from http.c and winhttp.c
bool git_http__expect_continue = false;
static const char* upload_pack_ls_service_url = "/info/refs?service=git-upload-pack";
static const char* upload_pack_service_url = "/git-upload-pack";
static const char* receive_pack_ls_service_url = "/info/refs?service=git-receive-pack";
static const char* receive_pack_service_url = "/git-receive-pack";

// Structs
typedef struct { git_smart_subtransport parent; transport_smart* owner; bool fake_ssh; } emhttp_subtransport;
typedef struct { git_smart_subtransport_stream parent; const char* service_url; int connection_id; } emhttp_stream;

// HTTP interface
static int emhttp_connection_read(int connId, const char* buf, size_t len) {
  int bytes_read = -2;
  MAIN_THREAD_ASYNC_EM_ASM({ readFromConnection($0, $1, $2, $3) }, connId, buf, len, &bytes_read);
  while (bytes_read == -2) { usleep(10); }
  return bytes_read;
}

static int emhttp_connection_alloc(char* url, bool is_post) {
  return MAIN_THREAD_EM_ASM_INT({ return createConnection(UTF8ToString($0), $1); }, url, is_post);
}

static int _emhttp_stream_read(git_smart_subtransport_stream* stream, char* buffer, size_t buf_size, size_t* bytes_read) {
  emhttp_stream* s = (emhttp_stream*) stream;
  if (s->connection_id == -1) {
    s->connection_id = emhttp_connection_alloc(s->service_url, false);
  }

  int read = emhttp_connection_read(s->connection_id, buffer, buf_size);
  if (read < 0) return read;
  *bytes_read = read;
  return 0;
}

static int _emhttp_stream_write(git_smart_subtransport_stream* stream, const char* buffer, size_t len) {
  emhttp_stream* s = (emhttp_stream*) stream;
  if (s->connection_id == -1) {
    s->connection_id = emhttp_connection_alloc(s->service_url, true);
  }

  return MAIN_THREAD_EM_ASM_INT({ return writeToConnection($0, $1, $2); }, s->connection_id, buffer, len);
}

static void _emhttp_stream_free(git_smart_subtransport_stream* stream) {
	emhttp_stream* s = (emhttp_stream*) stream;
  MAIN_THREAD_EM_ASM({ freeConnection($0); }, s->connection_id);
	git__free(s);
}

static int emhttp_stream_alloc(emhttp_subtransport* t, emhttp_stream** stream) {
  emhttp_stream* s;
	if (!stream) return -1;

  s = git__calloc(1, sizeof(emhttp_stream));
	GIT_ERROR_CHECK_ALLOC(s);

	s->parent.subtransport = &t->parent;
	s->parent.read = _emhttp_stream_read;
	s->parent.write = _emhttp_stream_write;
	s->parent.free = _emhttp_stream_free;
	s->connection_id = -1;
	*stream = s;
  return 0;
}

// Git transport
static int _emhttp_action(git_smart_subtransport_stream** stream, git_smart_subtransport* subtransport, char* url, git_smart_service_t action) {
  emhttp_subtransport* t = (emhttp_subtransport*) subtransport;
	emhttp_stream* s;

  if (emhttp_stream_alloc(t, &s) < 0) return -1;
  if (t->fake_ssh) {
    int c = 0;
    int len = 0;
    int pre = 0;
    while (url[c] != '\0') {
      if (len == 0) pre++;
      if (len != 0 || (len == 0 && url[c] == '@')) len++;
      c++;
    }

    const char* old_url = url;
    url = malloc((len + 8) * sizeof(char));
    url[0] = 'h'; url[1] = 't'; url[2] = 't'; url[3] = 'p'; url[4] = 's'; url[5] = ':'; url[6] = '/'; url[7] = '/';
    for (int i = 0; i < len; i++) url[i + 8] = old_url[i + pre] == ':' ? '/' : old_url[i + pre];
    url[(len + 8)] = '\0';
  }

  git_buf buf = GIT_BUF_INIT;
  if (action == GIT_SERVICE_UPLOADPACK_LS) {
    git_buf_printf(&buf, "%s%s", url, upload_pack_ls_service_url);
  } else if (action == GIT_SERVICE_UPLOADPACK) {
    git_buf_printf(&buf, "%s%s", url, upload_pack_service_url);
  } else if (action == GIT_SERVICE_RECEIVEPACK_LS) {
    git_buf_printf(&buf, "%s%s", url, receive_pack_ls_service_url);
  } else if (action == GIT_SERVICE_RECEIVEPACK) {
    git_buf_printf(&buf, "%s%s", url, receive_pack_service_url);
  } else {
    if (t->fake_ssh) free(url);
    return -1;
  }

  s->service_url = git_buf_cstr(&buf);
  *stream = &s->parent;
  if (t->fake_ssh) free(url);
  return 0;
}

static int _emhttp_close(git_smart_subtransport* subtransport) {
  // Nothing to do
  return 0;
}

static void _emhttp_free(git_smart_subtransport* subtransport) {
	emhttp_subtransport* t = (emhttp_subtransport*) subtransport;
	git__free(t);
}

int git_smart_subtransport_http(git_smart_subtransport** out, git_transport* owner, void* param) {
  emhttp_subtransport* t;
	if (!out) return -1;

	t = git__calloc(1, sizeof(emhttp_subtransport));
	GIT_ERROR_CHECK_ALLOC(t);

	t->owner = (transport_smart*) owner;
	t->parent.action = _emhttp_action;
	t->parent.close = _emhttp_close;
	t->parent.free = _emhttp_free;
	t->fake_ssh = *((int*) param) == 554;
	*out = (git_smart_subtransport*) t;
  return 0;
}
