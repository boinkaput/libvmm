/*
 * Copyright 2023, UNSW (ABN 57 195 873 179)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "../libsharedringbuffer/ringbuffer.h"

static inline int cmdq_enqueue(ring_buffer_t *ring, uintptr_t buffer, unsigned int len);
static inline int cmdq_dequeue();