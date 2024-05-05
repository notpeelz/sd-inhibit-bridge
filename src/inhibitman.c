#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <systemd/sd-bus.h>

#include "inhibitman.h"

typedef struct inhibitor {
  int fd;
  char const* who;
  char const* why;
} inhibitor_t;

typedef struct inhibitor_arr {
  inhibitor_t** items;
  size_t length;
  size_t capacity;
} inhibitor_arr_t;

struct inhibitman {
  sd_bus* system_bus;
  inhibitor_arr_t* inhibitors;
};

static size_t const DEFAULT_ARR_CAPACITY = 16;

static inhibitor_arr_t* inhibitor_arr_create() {
  inhibitor_arr_t* arr = calloc(1, sizeof(*arr));
  if (arr == nullptr) return nullptr;

  arr->capacity = DEFAULT_ARR_CAPACITY;
  arr->length = 0;
  arr->items = calloc(arr->capacity, sizeof(inhibitor_t));
  if (arr->items == nullptr) {
    free(arr);
    return nullptr;
  }

  return arr;
}

static void inhibitor_arr_destroy(inhibitor_arr_t* arr) {
  if (arr == nullptr) return;

  for (size_t i = 0; i < arr->length; i++) {
    if (arr->items[i] != nullptr) {
      inhibitor_t* inhibitor = arr->items[i];
      (void)close(inhibitor->fd);
      free((void*)inhibitor->who);
      free((void*)inhibitor->why);
      free((void*)inhibitor);
      arr->items[i] = nullptr;
    }
  }

  free(arr->items);
  free(arr);
}
SDIB_DEFINE_POINTER_CLEANUP_FUNC(inhibitor_arr_t, inhibitor_arr_destroy);

static int inhibitor_arr_add(
  inhibitor_arr_t* arr,
  int fd,
  char const* who,
  char const* why,
  size_t* idx
) {
  assert(arr != nullptr);
  assert(who != nullptr);
  assert(why != nullptr);
  assert(arr->length <= arr->capacity);

  inhibitor_t* inhibitor = calloc(1, sizeof(*inhibitor));
  if (inhibitor == nullptr) {
    return -ENOMEM;
  }

  inhibitor->fd = fd;
  inhibitor->who = strdup(who);
  inhibitor->why = strdup(why);

  for (size_t i = 0; i < arr->length; i++) {
    if (arr->items[i] == nullptr) {
      arr->items[i] = inhibitor;
      if (idx != nullptr) {
        *idx = i;
      }
      return 0;
    }
  }

  if (arr->length == arr->capacity) {
    size_t new_capacity = arr->capacity * 2;
    void* new_items = reallocarray(arr->items, new_capacity, sizeof(*arr->items));
    if (new_items == nullptr) {
      free(inhibitor);
      return -ENOMEM;
    }

    arr->items = new_items;
    arr->capacity = new_capacity;
  }

  arr->items[arr->length] = inhibitor;
  if (idx != nullptr) {
    *idx = arr->length;
  }
  arr->length++;

  return 0;
}

static bool inhibitor_arr_remove(inhibitor_arr_t* arr, size_t idx) {
  assert(arr != nullptr);
  assert(idx < arr->length);

  inhibitor_t* old = arr->items[idx];
  if (old == nullptr) {
    return false;
  }

  (void)close(old->fd);
  free((void*)old->who);
  free((void*)old->why);
  free(old);
  arr->items[idx] = nullptr;
  return true;
}

inhibitman_t* inhibitman_create(sd_bus* system_bus) {
  assert(system_bus != nullptr);

  inhibitman_t* im = calloc(1, sizeof(*im));
  if (im == nullptr) {
    return nullptr;
  }

  im->system_bus = sd_bus_ref(system_bus);
  im->inhibitors = inhibitor_arr_create();

  return im;
}

void inhibitman_destroy(inhibitman_t* im) {
  if (im != nullptr) {
    sd_bus_unrefp(&im->system_bus);
    inhibitor_arr_destroyp(&im->inhibitors);
    free(im);
  }
}

bool inhibitman_active(inhibitman_t* im) {
  assert(im != nullptr);

  for (size_t i = 0; i < im->inhibitors->length; i++) {
    if (im->inhibitors->items[i] != nullptr) {
      return true;
    }
  }

  return false;
}

int inhibitman_add(
  inhibitman_t* im,
  char const* who,
  char const* why,
  uint32_t* id
) {
  assert(im != nullptr);
  assert(who != nullptr);
  assert(why != nullptr);

  int fd = -1;
  int r;
  int err;

  _sdib_cleanup_(sd_bus_message_unrefp)
  sd_bus_message* reply = nullptr;

  r = sd_bus_call_method(
    im->system_bus,
    "org.freedesktop.login1",
    "/org/freedesktop/login1",
    "org.freedesktop.login1.Manager",
    "Inhibit",
    nullptr,
    &reply,
    "ssss",
    "idle",
    who,
    why,
    "block"
  );
  if (r < 0) {
    err = -r;
    goto fail;
  }

  r = sd_bus_message_read_basic(reply, 'h', &fd);
  if (r < 0) {
    err = -r;
    goto fail;
  }

  fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
  if (fd < 0) {
    err = errno;
    goto fail;
  }

  size_t idx;
  r = inhibitor_arr_add(im->inhibitors, fd, who, why, &idx);
  if (r < 0) {
    err = -r;
    goto fail;
  }

  // Valid ids range from 1 to UINT32_MAX
  if (idx > UINT32_MAX - 1) {
    (void)inhibitor_arr_remove(im->inhibitors, idx);
    err = EOVERFLOW;
    goto fail;
  }

  if (id != nullptr) {
    *id = (uint32_t)(idx + 1);
  }

  return 0;

fail:
  if (fd >= 0) {
    (void)close(fd);
  }

  return -err;
}

bool inhibitman_remove(inhibitman_t* im, uint32_t id) {
  assert(im != nullptr);

  if (id == 0) {
    return false;
  }

  if (id > UINT32_MAX - 1) {
    return false;
  }

  size_t idx = id - 1;
  if (idx >= im->inhibitors->length) {
    return false;
  }

  return inhibitor_arr_remove(im->inhibitors, idx);
}
