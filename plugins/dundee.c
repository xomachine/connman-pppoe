/*
 *
 *  Connection Manager
 *
 *  Copyright (C) 2012  BMW Car IT GmbH. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#include <gdbus.h>

#define CONNMAN_API_SUBJECT_TO_CHANGE
#include <connman/plugin.h>
#include <connman/device.h>
#include <connman/network.h>
#include <connman/dbus.h>

#define DUNDEE_SERVICE			"org.ofono.dundee"
#define DUNDEE_MANAGER_INTERFACE	DUNDEE_SERVICE ".Manager"

#define DEVICE_ADDED			"DeviceAdded"
#define DEVICE_REMOVED			"DeviceRemoved"

#define GET_DEVICES			"GetDevices"

#define TIMEOUT 40000

static DBusConnection *connection;

static GHashTable *dundee_devices = NULL;

struct dundee_data {
	char *path;
};

static void device_destroy(gpointer data)
{
	struct dundee_data *info = data;

	g_free(info->path);

	g_free(info);
}

static int network_probe(struct connman_network *network)
{
	DBG("network %p", network);

	return 0;
}

static void network_remove(struct connman_network *network)
{
	DBG("network %p", network);
}

static int network_connect(struct connman_network *network)
{
	DBG("network %p", network);

	return 0;
}

static int network_disconnect(struct connman_network *network)
{
	DBG("network %p", network);

	return 0;
}

static struct connman_network_driver network_driver = {
	.name		= "network",
	.type		= CONNMAN_NETWORK_TYPE_BLUETOOTH_DUN,
	.probe		= network_probe,
	.remove		= network_remove,
	.connect	= network_connect,
	.disconnect	= network_disconnect,
};

static int dundee_probe(struct connman_device *device)
{
	DBG("device %p", device);

	return 0;
}

static void dundee_remove(struct connman_device *device)
{
	DBG("device %p", device);
}

static int dundee_enable(struct connman_device *device)
{
	DBG("device %p", device);

	return 0;
}

static int dundee_disable(struct connman_device *device)
{
	DBG("device %p", device);

	return 0;
}

static struct connman_device_driver dundee_driver = {
	.name		= "dundee",
	.type		= CONNMAN_DEVICE_TYPE_BLUETOOTH,
	.probe		= dundee_probe,
	.remove		= dundee_remove,
	.enable		= dundee_enable,
	.disable	= dundee_disable,
};

static void add_device(const char *path, DBusMessageIter *properties)
{
	struct dundee_data *info;

	info = g_hash_table_lookup(dundee_devices, path);
	if (info != NULL)
		return;

	info = g_try_new0(struct dundee_data, 1);
	if (info == NULL)
		return;

	info->path = g_strdup(path);

	g_hash_table_insert(dundee_devices, g_strdup(path), info);
}

static gboolean device_added(DBusConnection *connection, DBusMessage *message,
				void *user_data)
{
	DBusMessageIter iter, properties;
	const char *path;
	const char *signature = DBUS_TYPE_OBJECT_PATH_AS_STRING
		DBUS_TYPE_ARRAY_AS_STRING
		DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
		DBUS_TYPE_STRING_AS_STRING
		DBUS_TYPE_VARIANT_AS_STRING
		DBUS_DICT_ENTRY_END_CHAR_AS_STRING;

	if (dbus_message_has_signature(message, signature) == FALSE) {
		connman_error("dundee signature does not match");
		return TRUE;
	}

	DBG("");

	if (dbus_message_iter_init(message, &iter) == FALSE)
		return TRUE;

	dbus_message_iter_get_basic(&iter, &path);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &properties);

	add_device(path, &properties);

	return TRUE;
}

static void remove_device(DBusConnection *connection, const char *path)
{
	DBG("path %s", path);

	g_hash_table_remove(dundee_devices, path);
}

static gboolean device_removed(DBusConnection *connection, DBusMessage *message,
				void *user_data)
{
	const char *path;
	const char *signature = DBUS_TYPE_OBJECT_PATH_AS_STRING;

	if (dbus_message_has_signature(message, signature) == FALSE) {
		connman_error("dundee signature does not match");
		return TRUE;
	}

	dbus_message_get_args(message, NULL, DBUS_TYPE_OBJECT_PATH, &path,
				DBUS_TYPE_INVALID);
	remove_device(connection, path);
	return TRUE;
}

static void manager_get_devices_reply(DBusPendingCall *call, void *user_data)
{
	DBusMessage *reply;
	DBusError error;
	DBusMessageIter array, dict;
	const char *signature = DBUS_TYPE_ARRAY_AS_STRING
		DBUS_STRUCT_BEGIN_CHAR_AS_STRING
		DBUS_TYPE_OBJECT_PATH_AS_STRING
		DBUS_TYPE_ARRAY_AS_STRING
		DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
		DBUS_TYPE_STRING_AS_STRING
		DBUS_TYPE_VARIANT_AS_STRING
		DBUS_DICT_ENTRY_END_CHAR_AS_STRING
		DBUS_STRUCT_END_CHAR_AS_STRING;

	DBG("");

	reply = dbus_pending_call_steal_reply(call);

	if (dbus_message_has_signature(reply, signature) == FALSE) {
		connman_error("dundee signature does not match");
		goto done;
	}

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, reply) == TRUE) {
		connman_error("%s", error.message);
		dbus_error_free(&error);
		goto done;
	}

	if (dbus_message_iter_init(reply, &array) == FALSE)
		goto done;

	dbus_message_iter_recurse(&array, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_STRUCT) {
		DBusMessageIter value, properties;
		const char *path;

		dbus_message_iter_recurse(&dict, &value);
		dbus_message_iter_get_basic(&value, &path);

		dbus_message_iter_next(&value);
		dbus_message_iter_recurse(&value, &properties);

		add_device(path, &properties);

		dbus_message_iter_next(&dict);
	}

done:
	dbus_message_unref(reply);

	dbus_pending_call_unref(call);
}

static int manager_get_devices(void)
{
	DBusMessage *message;
	DBusPendingCall *call;

	DBG("");

	message = dbus_message_new_method_call(DUNDEE_SERVICE, "/",
					DUNDEE_MANAGER_INTERFACE, GET_DEVICES);
	if (message == NULL)
		return -ENOMEM;

	if (dbus_connection_send_with_reply(connection, message,
						&call, TIMEOUT) == FALSE) {
		connman_error("Failed to call GetDevices()");
		dbus_message_unref(message);
		return -EINVAL;
	}

	if (call == NULL) {
		connman_error("D-Bus connection not available");
		dbus_message_unref(message);
		return -EINVAL;
	}

	dbus_pending_call_set_notify(call, manager_get_devices_reply,
					NULL, NULL);

	dbus_message_unref(message);

	return -EINPROGRESS;
}

static void dundee_connect(DBusConnection *connection, void *user_data)
{
	DBG("connection %p", connection);

	dundee_devices = g_hash_table_new_full(g_str_hash, g_str_equal,
					g_free, device_destroy);

	manager_get_devices();
}

static void dundee_disconnect(DBusConnection *connection, void *user_data)
{
	DBG("connection %p", connection);

	g_hash_table_destroy(dundee_devices);
	dundee_devices = NULL;
}

static guint watch;
static guint added_watch;
static guint removed_watch;

static int dundee_init(void)
{
	int err;

	connection = connman_dbus_get_connection();
	if (connection == NULL)
		return -EIO;

	watch = g_dbus_add_service_watch(connection, DUNDEE_SERVICE,
			dundee_connect, dundee_disconnect, NULL, NULL);

	added_watch = g_dbus_add_signal_watch(connection, NULL, NULL,
						DUNDEE_MANAGER_INTERFACE,
						DEVICE_ADDED, device_added,
						NULL, NULL);

	removed_watch = g_dbus_add_signal_watch(connection, NULL, NULL,
						DUNDEE_MANAGER_INTERFACE,
						DEVICE_REMOVED, device_removed,
						NULL, NULL);

	if (watch == 0 || added_watch == 0 || removed_watch == 0) {
		err = -EIO;
		goto remove;
	}

	err = connman_network_driver_register(&network_driver);
	if (err < 0)
		goto remove;

	err = connman_device_driver_register(&dundee_driver);
	if (err < 0) {
		connman_network_driver_unregister(&network_driver);
		goto remove;
	}

	return 0;

remove:
	g_dbus_remove_watch(connection, watch);
	g_dbus_remove_watch(connection, added_watch);
	g_dbus_remove_watch(connection, removed_watch);

	dbus_connection_unref(connection);

	return err;
}

static void dundee_exit(void)
{
	g_dbus_remove_watch(connection, watch);
	g_dbus_remove_watch(connection, added_watch);
	g_dbus_remove_watch(connection, removed_watch);

	connman_device_driver_unregister(&dundee_driver);
	connman_network_driver_unregister(&network_driver);

	dbus_connection_unref(connection);
}

CONNMAN_PLUGIN_DEFINE(dundee, "Dundee plugin", VERSION,
		CONNMAN_PLUGIN_PRIORITY_DEFAULT, dundee_init, dundee_exit)
