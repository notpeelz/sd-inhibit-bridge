#ifndef SDIB_INHIBITMAN_H
#define SDIB_INHIBITMAN_H

#include <stdint.h>
#include <systemd/sd-bus.h>

typedef struct inhibitman inhibitman_t;

inhibitman_t* inhibitman_create(sd_bus* system_bus);

void inhibitman_destroy(inhibitman_t* im);
SDIB_DEFINE_POINTER_CLEANUP_FUNC(inhibitman_t, inhibitman_destroy)

bool inhibitman_active(inhibitman_t* im);

int inhibitman_add(
  inhibitman_t* im,
  char const* who,
  char const* why,
  uint32_t* id
);

bool inhibitman_remove(
  inhibitman_t* im,
  uint32_t id
);

#endif
