#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-bus.h>

#include "inhibitman.h"
#include "htable.h"
#include "config.h"

typedef struct peer {
  char const* name;
  inhibitman_t* im;
} peer_t;

static peer_t* peer_create(char const* name, sd_bus* system_bus) {
  peer_t* peer = nullptr;
  inhibitman_t* im = nullptr;
  char const* peer_name = nullptr;

  peer = calloc(1, sizeof(*peer));
  if (peer == nullptr) goto fail;

  im = inhibitman_create(system_bus);
  if (im == nullptr) goto fail;

  peer_name = (char const*)strdup(name);
  if (peer_name == nullptr) goto fail;

  peer->name = peer_name;
  peer->im = im;

  return peer;

fail:
  inhibitman_destroyp(&im);
  free((void*)peer_name);
  free(peer);
  return nullptr;
}

static void peer_destroy(peer_t* peer) {
  if (peer != nullptr) {
    fprintf(
      stderr,
      SD_DEBUG "destroying peer\n"
      SD_DEBUG "  name=%s\n",
      peer->name
    );
    inhibitman_destroyp(&peer->im);
    free((void*)peer->name);
    free(peer);
  }
}
SDIB_DEFINE_POINTER_CLEANUP_FUNC(peer_t, peer_destroy);

typedef struct bus_context {
  htable_t* peers;
  sd_bus* system_bus;
} bus_context_t;

static uint64_t peer_htable_hash(void const* in) {
  uint64_t hash = 0xcbf29ce484222325u;
  for (char const* k = in; *k != '\0'; k++) {
    hash ^= *k;
    hash *= 0x100000001b3u;
  }
  return hash;
}

static bool peer_htable_keq(void const* a, void const* b) {
  return strcmp(a, b) == 0;
}

static void* inhibitor_htable_kcopy(void* in) {
  auto k = (char const*)in;
  return strdup(k);
}

static void inhibitor_htable_kfree(void* in) {
  free(in);
}

static void* inhibitor_htable_vcopy(void* in) {
  return in;
}

static void inhibitor_htable_vfree(void* in) {
  auto peer = (peer_t*)in;
  peer_destroyp(&peer);
}

static htable_callbacks_t inhibitor_htable_callbacks = {
  .kcopy = inhibitor_htable_kcopy,
  .kfree = inhibitor_htable_kfree,
  .vcopy = inhibitor_htable_vcopy,
  .vfree = inhibitor_htable_vfree,
};

static bus_context_t* bus_context_create(sd_bus* system_bus) {
  assert(system_bus != nullptr);

  bus_context_t* ctx = nullptr;
  htable_t* ht = nullptr;
  system_bus = sd_bus_ref(system_bus);

  ctx = calloc(1, sizeof(*ctx));
  if (ctx == nullptr) goto fail;

  ht = htable_create(
    peer_htable_hash,
    peer_htable_keq,
    &inhibitor_htable_callbacks
  );
  if (ht == nullptr) goto fail;

  ctx->peers = ht;
  ctx->system_bus = system_bus;
  return ctx;

fail:
  free(ctx);
  free(ht);
  sd_bus_unrefp(&system_bus);
  return nullptr;
}

static void bus_context_destroy(bus_context_t* ctx) {
  if (ctx == nullptr) return;
  htable_destroyp(&ctx->peers);
  sd_bus_unrefp(&ctx->system_bus);
  free(ctx);
}
SDIB_DEFINE_POINTER_CLEANUP_FUNC(bus_context_t, bus_context_destroy);

static int bus_context_get_peer(
  bus_context_t* ctx,
  char const* name,
  peer_t** peer
) {
  assert(ctx != nullptr);
  assert(name != nullptr);
  assert(peer != nullptr);

  if (!htable_get(ctx->peers, name, (void**)peer)) {
    *peer = peer_create(name, ctx->system_bus);
    if (*peer == nullptr) {
      return -ENOMEM;
    }

    htable_insert(ctx->peers, (void*)name, *peer);
  }

  assert((*peer)->im != nullptr);
  assert((*peer)->name != nullptr);

  return 0;
}

static bool bus_context_remove_peer(
  bus_context_t* ctx,
  char const* name
) {
  assert(ctx != nullptr);
  assert(name != nullptr);

  peer_t* peer;
  if (htable_remove(ctx->peers, name, (void**)&peer)) {
    if (inhibitman_active(peer->im)) {
      fprintf(
        stderr,
        SD_DEBUG "cleaning up lingering inhibitors\n"
        SD_DEBUG "  peer=%s\n",
        name
      );
    }
    peer_destroyp(&peer);
    return true;
  }

  return false;
}

static int setup_signal_handlers(sd_event* event) {
  assert(event != nullptr);

  int r;
  sigset_t ss;

  if (
    sigemptyset(&ss) < 0
    || sigaddset(&ss, SIGTERM) < 0
    || sigaddset(&ss, SIGINT) < 0
  ) {
    return -1;
  }

  if (sigprocmask(SIG_BLOCK, &ss, nullptr) < 0) {
    return -1;
  }

  r = sd_event_add_signal(event, nullptr, SIGTERM, nullptr, nullptr);
  if (r < 0) {
    return -1;
  }

  r = sd_event_add_signal(event, nullptr, SIGINT, nullptr, nullptr);
  if (r < 0) {
    return -1;
  }

  return 0;
}

static int method_inhibit(
  sd_bus_message* m,
  void* userdata,
  sd_bus_error* err
) {
  (void)err;

  int r;
  auto ctx = (bus_context_t*)userdata;

  char const* sender = sd_bus_message_get_sender(m);

  char* app_name;
  r = sd_bus_message_read_basic(m, 's', &app_name);
  if (r < 0) return r;

  char* reason;
  r = sd_bus_message_read_basic(m, 's', &reason);
  if (r < 0) return r;

  peer_t* peer;
  r = bus_context_get_peer(ctx, sender, &peer);
  if (r < 0) return r;

  uint32_t id;
  r = inhibitman_add(peer->im, app_name, reason, &id);
  if (r < 0) {
    fprintf(
      stderr,
      SD_ERR "inhibit: %s\n"
      SD_ERR "  peer=%s\n"
      SD_ERR "  app_name=%s\n"
      SD_ERR "  reason=%s\n",
      strerror(-r),
      sender,
      app_name,
      reason
    );
    return sd_bus_reply_method_errnof(m, r, "failed to add inhibitor: %m");
  }

  fprintf(
    stderr,
    SD_DEBUG "inhibit\n"
    SD_DEBUG "  peer=%s\n"
    SD_DEBUG "  app_name=%s\n"
    SD_DEBUG "  reason=%s\n"
    SD_DEBUG "  cookie=%u\n",
    sender,
    app_name,
    reason,
    id
  );

  return sd_bus_reply_method_return(m, "u", id);
}

static int method_uninhibit(
  sd_bus_message* m,
  void* userdata,
  sd_bus_error* err
) {
  (void)err;

  auto ctx = (bus_context_t*)userdata;
  int r;

  char const* sender = sd_bus_message_get_sender(m);

  uint32_t id;
  r = sd_bus_message_read_basic(m, 'u', &id);
  if (r < 0) return r;

  peer_t* peer;
  r = bus_context_get_peer(ctx, sender, &peer);
  if (r < 0) return r;

  if (!inhibitman_remove(peer->im, id)) {
    fprintf(
      stderr,
      SD_ERR "uninhibit: invalid cookie\n"
      SD_ERR "  peer=%s\n"
      SD_ERR "  cookie=%u\n",
      sender,
      id
    );
    return sd_bus_reply_method_errnof(m, EINVAL, "invalid cookie");
  }

  fprintf(
    stderr,
    SD_DEBUG "uninhibit\n"
    SD_DEBUG "  peer=%s\n"
    SD_DEBUG "  cookie=%u\n",
    sender,
    id
  );

  return sd_bus_reply_method_return(m, "");
}

static sd_bus_vtable const screensaver_vtable[] = {
  SD_BUS_VTABLE_START(0),
  SD_BUS_METHOD_WITH_ARGS(
    "Inhibit",
    SD_BUS_ARGS(
      "s", app_name,
      "s", reason
    ),
    SD_BUS_RESULT("u", cookie),
    method_inhibit,
    0
  ),
  SD_BUS_METHOD_WITH_ARGS(
    "UnInhibit",
    SD_BUS_ARGS("u", cookie),
    SD_BUS_NO_RESULT,
    method_uninhibit,
    0
  ),
  SD_BUS_VTABLE_END,
};

static int on_name_owner_changed(
  sd_bus_message* m,
  void* userdata,
  sd_bus_error* ret_error
) {
  (void)ret_error;

  int r;
  auto ctx = (bus_context_t*)userdata;

  char* name;
  char* old_owner;
  char* new_owner;

  r = sd_bus_message_read_basic(m, 's', &name);
  if (r < 0) return r;

  r = sd_bus_message_read_basic(m, 's', &old_owner);
  if (r < 0) return r;

  r = sd_bus_message_read_basic(m, 's', &new_owner);
  if (r < 0) return r;

  if (strcmp(name, old_owner) == 0 && strcmp(new_owner, "") == 0) {
    // fprintf(
    //   stderr,
    //   SD_DEBUG "name disappeared from bus\n"
    //   SD_DEBUG "  name=%s\n",
    //   name
    // );
    (void)bus_context_remove_peer(ctx, name);
  }

  return 0;
}

static int setup_screensaver_service(sd_bus* bus, bus_context_t* ctx) {
  assert(bus != nullptr);
  assert(ctx != nullptr);

  int r;

  r = sd_bus_match_signal(
    bus,
    nullptr,
    "org.freedesktop.DBus",
    "/org/freedesktop/DBus",
    "org.freedesktop.DBus",
    "NameOwnerChanged",
    on_name_owner_changed,
    ctx
  );
  if (r < 0) goto fail;

  r = sd_bus_add_object_vtable(
    bus,
    nullptr,
    "/org/freedesktop/ScreenSaver",
    "org.freedesktop.ScreenSaver",
    screensaver_vtable,
    ctx
  );
  if (r < 0) goto fail;

  r = sd_bus_request_name(
    bus,
    "org.freedesktop.ScreenSaver",
    0
  );
  if (r < 0) {
    fprintf(
      stderr,
      SD_ERR "failed to acquire name %s: %s\n",
      SD_ERR "org.freedesktop.ScreenSaver",
      strerror(-r)
    );
    goto fail;
  }

  return 0;
fail:
  return -1;
}

static struct option long_options[] = {
  {"help", no_argument, nullptr, 'h'},
  {"version", no_argument, nullptr, 'V'},
  {0},
};

static char usage[] = {
  "Usage: sd-inhibit-bridge [options]\n"
  "\n"
  "  -h, --help    "
  "Print help\n"
  "  -V, --version "
  "Print version\n"
};

static int parse_options(int argc, char* argv[], int* exitcode) {
  assert(argv != nullptr);
  assert(exitcode != nullptr);

  optind = 1;
  while (true) {
    int c = getopt_long(argc, argv, "hVv", long_options, nullptr);
    if (c < 0) {
      break;
    }

    switch (c) {
      case 'V': {
        fprintf(stderr, "sd-inhibit-bridge version %s\n", SDIB_VERSION);
        goto exit_success;
      }
      case 'h': {
        fprintf(stderr, "%s", usage);
        goto exit_success;
      }
      default: {
        goto exit_fail;
      }
    }
  }

  if (argc > optind) {
    goto exit_fail;
  }

  return 0;

exit_success:
  *exitcode = EXIT_SUCCESS;
  return -1;

exit_fail:
  fprintf(stderr, "%s", usage);
  *exitcode = EXIT_FAILURE;
  return -1;
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  int ret = EXIT_FAILURE;
  int r;

  _sdib_cleanup_(sd_bus_unrefp)
  sd_bus* user_bus = nullptr;

  _sdib_cleanup_(sd_bus_unrefp)
  sd_bus* system_bus = nullptr;

  _sdib_cleanup_(bus_context_destroyp)
  bus_context_t* ctx = nullptr;

  _sdib_cleanup_(sd_event_unrefp)
  sd_event* event = nullptr;

  r = parse_options(argc, argv, &ret);
  if (r < 0) goto ret;

  r = sd_event_default(&event);
  if (r < 0) goto ret;

  r = setup_signal_handlers(event);
  if (r < 0) goto ret;

  r = sd_bus_open_user(&user_bus);
  if (r < 0) {
    fprintf(
      stderr,
      SD_ERR "failed to connect to user bus: %s\n",
      strerror(-r)
    );
    goto ret;
  }

  r = sd_bus_open_system(&system_bus);
  if (r < 0) {
    fprintf(
      stderr,
      SD_ERR "failed to connect to system bus: %s\n",
      strerror(-r)
    );
    goto ret;
  }

  ctx = bus_context_create(system_bus);
  if (ctx == nullptr) goto ret;

  r = setup_screensaver_service(user_bus, ctx);
  if (r < 0) goto ret;

  r = sd_bus_attach_event(system_bus, event, SD_EVENT_PRIORITY_NORMAL);
  if (r < 0) goto ret;

  r = sd_bus_attach_event(user_bus, event, SD_EVENT_PRIORITY_NORMAL);
  if (r < 0) goto ret;

  r = sd_event_loop(event);
  if (r < 0) {
    fprintf(
      stderr,
      SD_ERR "sd_event_loop failed: %s\n",
      strerror(-r)
    );
    goto ret;
  }

  ret = EXIT_SUCCESS;

ret:
  return ret;
}
