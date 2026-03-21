#pragma once

/**
 * Minimal wayland-util definitions required by wayland-scanner generated code.
 * Full specification: https://wayland.freedesktop.org/docs/html/
 */

struct wl_message {
  const char*                 name;
  const char*                 signature;
  const struct wl_interface** types;
};

struct wl_interface {
  const char*              name;
  int                      version;
  int                      method_count;
  const struct wl_message* methods;
  int                      event_count;
  const struct wl_message* events;
};
