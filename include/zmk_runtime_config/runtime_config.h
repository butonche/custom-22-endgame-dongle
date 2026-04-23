/*
 * Stub for zmk-runtime-config — returns compile-time defaults.
 * Allows upstream efogdev drivers (ec11-ish) to build without
 * the full zmk-runtime-config module.
 */
#pragma once

#define ZRC_GET(key, default_val) (default_val)

static inline void zrc_register(const char *key, int val, int min, int max) {}
