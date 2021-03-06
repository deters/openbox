/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   obt/xevent.c for the Openbox window manager
   Copyright (c) 2007        Dana Jansens

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "obt/xevent.h"
#include "obt/mainloop.h"
#include "obt/util.h"

typedef struct _ObtXEventBinding ObtXEventBinding;

struct _ObtXEventHandler
{
    gint ref;
    ObtMainLoop *loop;

    /* An array of hash tables where the key is the window, and the value is
       the ObtXEventBinding */
    GHashTable **bindings;
    gint num_event_types; /* the length of the bindings array */
};

struct _ObtXEventBinding
{
    Window win;
    ObtXEventCallback func;
    gpointer data;
};

static void xevent_handler(const XEvent *e, gpointer data);
static guint window_hash(Window *w) { return *w; }
static gboolean window_comp(Window *w1, Window *w2) { return *w1 == *w2; }
static void binding_free(gpointer b);

ObtXEventHandler* xevent_new(void)
{
    ObtXEventHandler *h;

    h = g_slice_new0(ObtXEventHandler);
    h->ref = 1;

    return h;
}

void xevent_ref(ObtXEventHandler *h)
{
    ++h->ref;
}

void xevent_unref(ObtXEventHandler *h)
{
    if (h && --h->ref == 0) {
        gint i;

        if (h->loop)
            obt_main_loop_x_remove(h->loop, xevent_handler);
        for (i = 0; i < h->num_event_types; ++i)
            g_hash_table_destroy(h->bindings[i]);
        g_free(h->bindings);

        g_slice_free(ObtXEventHandler, h);
    }
}

void xevent_register(ObtXEventHandler *h, ObtMainLoop *loop)
{
    h->loop = loop;
    obt_main_loop_x_add(loop, xevent_handler, h, NULL);
}

void xevent_set_handler(ObtXEventHandler *h, gint type, Window win,
                        ObtXEventCallback func, gpointer data)
{
    ObtXEventBinding *b;

    g_assert(func);

    /* make sure we have a spot for the event */
    if (type + 1 < h->num_event_types) {
        gint i;
        h->bindings = g_renew(GHashTable*, h->bindings, type + 1);
        for (i = h->num_event_types; i < type + 1; ++i)
            h->bindings[i] = g_hash_table_new_full((GHashFunc)window_hash,
                                                   (GEqualFunc)window_comp,
                                                   NULL, binding_free);
        h->num_event_types = type + 1;
    }

    b = g_slice_new(ObtXEventBinding);
    b->win = win;
    b->func = func;
    b->data = data;
    g_hash_table_replace(h->bindings[type], &b->win, b);
}

static void binding_free(gpointer b)
{
    g_slice_free(ObtXEventBinding, b);
}

void xevent_remove_handler(ObtXEventHandler *h, gint type, Window win)
{
    g_assert(type < h->num_event_types);
    g_assert(win);

    g_hash_table_remove(h->bindings[type], &win);
}

static void xevent_handler(const XEvent *e, gpointer data)
{
    ObtXEventHandler *h;
    ObtXEventBinding *b;

    h = data;

    if (e->type < h->num_event_types) {
        const gint all = OBT_XEVENT_ALL_WINDOWS;
        /* run the all_windows handler first */
        b = g_hash_table_lookup(h->bindings[e->xany.type], &all);
        if (b) b->func(e, b->data);
        /* then run the per-window handler */
        b = g_hash_table_lookup(h->bindings[e->xany.type], &e->xany.window);
        if (b) b->func(e, b->data);
    }
    else
        g_message("Unhandled X Event type %d", e->xany.type);
}
