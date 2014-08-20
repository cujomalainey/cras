/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_DEVICE_H_
#define CRAS_BT_DEVICE_H_

#include <dbus/dbus.h>

struct cras_bt_adapter;
struct cras_bt_device;
struct cras_iodev;

enum cras_bt_device_profile {
	CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE	= (1 << 0),
	CRAS_BT_DEVICE_PROFILE_A2DP_SINK	= (1 << 1),
	CRAS_BT_DEVICE_PROFILE_AVRCP_REMOTE	= (1 << 2),
	CRAS_BT_DEVICE_PROFILE_AVRCP_TARGET	= (1 << 3),
	CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE	= (1 << 4),
	CRAS_BT_DEVICE_PROFILE_HFP_AUDIOGATEWAY	= (1 << 5),
	CRAS_BT_DEVICE_PROFILE_HSP_HEADSET	= (1 << 6),
	CRAS_BT_DEVICE_PROFILE_HSP_AUDIOGATEWAY = (1 << 7)
};

enum cras_bt_device_profile cras_bt_device_profile_from_uuid(const char *uuid);

struct cras_bt_device *cras_bt_device_create(const char *object_path);
void cras_bt_device_destroy(struct cras_bt_device *device);
void cras_bt_device_reset();

struct cras_bt_device *cras_bt_device_get(const char *object_path);
size_t cras_bt_device_get_list(struct cras_bt_device ***device_list_out);

const char *cras_bt_device_object_path(const struct cras_bt_device *device);
struct cras_bt_adapter *cras_bt_device_adapter(
	const struct cras_bt_device *device);
const char *cras_bt_device_address(const struct cras_bt_device *device);
const char *cras_bt_device_name(const struct cras_bt_device *device);
int cras_bt_device_paired(const struct cras_bt_device *device);
int cras_bt_device_trusted(const struct cras_bt_device *device);
int cras_bt_device_connected(const struct cras_bt_device *device);

int cras_bt_device_supports_profile(const struct cras_bt_device *device,
				    enum cras_bt_device_profile profile);

void cras_bt_device_update_properties(struct cras_bt_device *device,
				      DBusMessageIter *properties_array_iter,
				      DBusMessageIter *invalidated_array_iter);

/* Gets the SCO socket for the device.
 * Args:
 *     device - The device object to get SCO socket for.
 */
int cras_bt_device_sco_connect(struct cras_bt_device *device);

/* Appends an iodev to bt device.
 * Args:
 *    device - The device to append iodev to.
 *    iodev - The iodev to add.
 *    profile - The profile of the iodev about to add.
 */
void cras_bt_device_append_iodev(struct cras_bt_device *device,
				 struct cras_iodev *iodev,
				 enum cras_bt_device_profile profile);

/* Removes an iodev from bt device.
 * Args:
 *    device - The device to remove iodev from.
 *    iodev - The iodev to remove.
 */
void cras_bt_device_rm_iodev(struct cras_bt_device *device,
			     struct cras_iodev *iodev);

#endif /* CRAS_BT_DEVICE_H_ */
