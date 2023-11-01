/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2020 Canonical Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#ifdef HAVE_NETWORK_MANAGER
#  include <NetworkManager.h>
#endif

#include "shell/cc-object-storage.h"

#include "cc-connectivity-page.h"

#include <glib/gi18n.h>

struct _CcConnectivityPage
{
  AdwNavigationPage parent_instance;

  GtkWidget *connectivity_switch;

#ifdef HAVE_NETWORK_MANAGER
  GCancellable *cancellable;
  NMClient     *nm_client;
#endif
};

G_DEFINE_TYPE (CcConnectivityPage, cc_connectivity_page, ADW_TYPE_NAVIGATION_PAGE)

static void
setup_nm_client (CcConnectivityPage *self,
                 NMClient           *client)
{
  self->nm_client = client;

  g_object_bind_property (self->nm_client, NM_CLIENT_CONNECTIVITY_CHECK_AVAILABLE,
                          self->connectivity_switch, "sensitive",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (self->nm_client, NM_CLIENT_CONNECTIVITY_CHECK_ENABLED,
                          self->connectivity_switch, "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
}

static void
nm_client_ready_cb (GObject *source_object,
                    GAsyncResult *res,
                    gpointer user_data)
{
  CcConnectivityPage *self;
  NMClient *client;
  g_autoptr(GError) error = NULL;

  client = nm_client_new_finish (res, &error);
  if (!client)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to create NetworkManager client: %s",
                   error->message);
      return;
    }

  self = user_data;

  /* Setup the client */
  setup_nm_client (self, client);

  /* Store the object in the cache too */
  cc_object_storage_add_object (CC_OBJECT_NMCLIENT, client);
}

static void
cc_connectivity_page_dispose (GObject *object)
{
  CcConnectivityPage *self = CC_CONNECTIVITY_PAGE (object);

  g_cancellable_cancel (self->cancellable);

#ifdef HAVE_NETWORK_MANAGER
  g_clear_object (&self->nm_client);
#endif

  G_OBJECT_CLASS (cc_connectivity_page_parent_class)->dispose (object);
}

static void
cc_connectivity_page_class_init (CcConnectivityPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_connectivity_page_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/privacy/cc-connectivity-page.ui");

  gtk_widget_class_bind_template_child (widget_class, CcConnectivityPage, connectivity_switch);
}

static void
cc_connectivity_page_init (CcConnectivityPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();

#ifdef HAVE_NETWORK_MANAGER
  /* Create and store a NMClient instance if it doesn't exist yet */
  if (cc_object_storage_has_object (CC_OBJECT_NMCLIENT))
    setup_nm_client (self, cc_object_storage_get_object (CC_OBJECT_NMCLIENT));
  else
    nm_client_new_async (self->cancellable, nm_client_ready_cb, self);
#endif
}
