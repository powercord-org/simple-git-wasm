// Replacement for libgit2 http transport, that uses js bindings to node' http api

#include <emscripten.h>
#include <git2/transport.h>
#include "smart.h"

#define EMHTTP_ALLOC_CONNECTION(url, isPost) \
  EM_ASM_INT({ return Module.createConnection(UTF8ToString($0), $1); }, url, isPost)

// Constants -- copied from http.c and winhttp.c
bool git_http__expect_continue = false;
static const char* upload_pack_ls_service_url = "/info/refs?service=git-upload-pack";
static const char* upload_pack_service_url = "/git-upload-pack";
static const char* receive_pack_ls_service_url = "/info/refs?service=git-receive-pack";
static const char* receive_pack_service_url = "/git-receive-pack";

// Structs
typedef struct { git_smart_subtransport parent; transport_smart* owner; } emhttp_subtransport;
typedef struct { git_smart_subtransport_stream parent; const char* service_url; int connectionId; } emhttp_stream;

// HTTP interface -- only read is an async op, everything else can be treated as synchronous.
EM_JS(int, _emhttp_js_read, (int connId, const char* buffer, size_t len), {
  return Asyncify.handleAsync(() => Module.readFromConnection(connId, buffer, len));
});

static int _emhttp_stream_read (git_smart_subtransport_stream* stream, char* buffer, size_t buf_size, size_t* bytes_read) {
  emhttp_stream* s = (emhttp_stream*) stream;
  if (s->connectionId == -1) {
    s->connectionId = EMHTTP_ALLOC_CONNECTION(s->service_url, false);
  }

  *bytes_read = _emhttp_js_read(s->connectionId, buffer, buf_size);
  return 0;
}

static int _emhttp_stream_write (git_smart_subtransport_stream* stream, const char* buffer, size_t len) {
  emhttp_stream* s = (emhttp_stream*) stream;
  if (s->connectionId == -1) {
    s->connectionId = EMHTTP_ALLOC_CONNECTION(s->service_url, true);
  }

  return EM_ASM_INT({ return Module.writeToConnection($0, $1, $2); }, s->connectionId, buffer, len);
}

static void _emhttp_stream_free (git_smart_subtransport_stream* stream) {
	emhttp_stream* s = (emhttp_stream*) stream;
  EM_ASM({ Module.freeConnection($0); }, s->connectionId);
	git__free(s);
}

static int emhttp_stream_alloc (emhttp_subtransport* t, emhttp_stream** stream) {
  emhttp_stream* s;
	if (!stream) return -1;

  s = git__calloc(1, sizeof(emhttp_stream));
	GIT_ERROR_CHECK_ALLOC(s);

	s->parent.subtransport = &t->parent;
	s->parent.read = _emhttp_stream_read;
	s->parent.write = _emhttp_stream_write;
	s->parent.free = _emhttp_stream_free;
	s->connectionId = -1;
	*stream = s;
  return 0;
}

// Git transport
static int _emhttp_action (git_smart_subtransport_stream** stream, git_smart_subtransport* subtransport, const char* url, git_smart_service_t action) {
  emhttp_subtransport* t = (emhttp_subtransport*) subtransport;
	emhttp_stream* s;

  if (emhttp_stream_alloc(t, &s) < 0) return -1;

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
    return -1;
  }

  s->service_url = git_buf_cstr(&buf);
  *stream = &s->parent;
  return 0;
}

static int _emhttp_close (git_smart_subtransport* subtransport) {
  // Nothing to do
  return 0;
}

static void _emhttp_free (git_smart_subtransport* subtransport) {
	emhttp_subtransport* t = (emhttp_subtransport*) subtransport;
	git__free(t);
}

int git_smart_subtransport_http (git_smart_subtransport** out, git_transport* owner, void* param) {
  emhttp_subtransport* t;
	GIT_UNUSED(param);
	if (!out) return -1;

	t = git__calloc(1, sizeof(emhttp_subtransport));
	GIT_ERROR_CHECK_ALLOC(t);

	t->owner = (transport_smart*) owner;
	t->parent.action = _emhttp_action;
	t->parent.close = _emhttp_close;
	t->parent.free = _emhttp_free;
	*out = (git_smart_subtransport*) t;
  return 0;
}
