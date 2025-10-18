/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Virtio message transport header.
 *
 * Copyright (c) 2025 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>
 *
 * Copyright (C) 2025 Google LLC and Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 */

#ifndef _LINUX_VIRTIO_MSG_H
#define _LINUX_VIRTIO_MSG_H

#include <linux/types.h>

/* Virtio message transport definitions */

/* Message types */
#define VIRTIO_MSG_DEVICE_INFO			0x02
#define VIRTIO_MSG_GET_DEV_FEATURES		0x03
#define VIRTIO_MSG_SET_DRV_FEATURES		0x04
#define VIRTIO_MSG_GET_CONFIG			0x05
#define VIRTIO_MSG_SET_CONFIG			0x06
#define VIRTIO_MSG_GET_DEVICE_STATUS		0x07
#define VIRTIO_MSG_SET_DEVICE_STATUS		0x08
#define VIRTIO_MSG_GET_VQUEUE			0x09
#define VIRTIO_MSG_SET_VQUEUE			0x0a
#define VIRTIO_MSG_RESET_VQUEUE			0x0b
#define VIRTIO_MSG_GET_SHM			0x0c
#define VIRTIO_MSG_EVENT_CONFIG			0x40
#define VIRTIO_MSG_EVENT_AVAIL			0x41
#define VIRTIO_MSG_EVENT_USED			0x42
#define VIRTIO_MSG_MAX				VIRTIO_MSG_EVENT_USED

#define VIRTIO_MSG_MIN_SIZE			44
#define VIRTIO_MSG_MAX_SIZE			65536
#define VIRTIO_MSG_REVISION_1			0x1

/* Message payload format */

struct get_device_info_resp {
	__le32 device_id;
	__le32 vendor_id;
	__le32 num_feature_bits;
	__le32 config_size;
	__le32 max_vq_count;
	__le16 admin_vq_start_idx;
	__le16 admin_vq_count;
} __attribute__((packed));

struct get_features {
	__le32 index;
	__le32 num;
} __attribute__((packed));

struct get_features_resp {
	__le32 index;
	__le32 num;
	__u8 features[];
} __attribute__((packed));

struct set_features {
	__le32 index;
	__le32 num;
	__u8 features[];
} __attribute__((packed));

struct get_config {
	__le32 offset;
	__le32 size;
} __attribute__((packed));

struct get_config_resp {
	__le32 generation;
	__le32 offset;
	__le32 size;
	__u8 config[];
} __attribute__((packed));

struct set_config {
	__le32 generation;
	__le32 offset;
	__le32 size;
	__u8 config[];
} __attribute__((packed));

struct set_config_resp {
	__le32 generation;
	__le32 offset;
	__le32 size;
	__u8 config[];
} __attribute__((packed));

struct get_device_status_resp {
	__le32 status;
} __attribute__((packed));

struct set_device_status {
	__le32 status;
} __attribute__((packed));

struct set_device_status_resp {
	__le32 status;
} __attribute__((packed));

struct get_vqueue {
	__le32 index;
} __attribute__((packed));

struct get_vqueue_resp {
	__le32 index;
	__le32 max_size;
	__le32 size;
	__le64 descriptor_addr;
	__le64 driver_addr;
	__le64 device_addr;
} __attribute__((packed));

struct set_vqueue {
	__le32 index;
	__le32 unused;
	__le32 size;
	__le64 descriptor_addr;
	__le64 driver_addr;
	__le64 device_addr;
} __attribute__((packed));

struct reset_vqueue {
	__le32 index;
} __attribute__((packed));

struct get_shm {
	__le32 index;
} __attribute__((packed));

struct get_shm_resp {
	__le32 index;
	__le32 count;
	__le32 addr;
} __attribute__((packed));

struct event_config {
	__le32 status;
	__le32 generation;
	__le32 offset;
	__le32 size;
	__u8 config[];
} __attribute__((packed));

struct event_avail {
	__le32 index;
	#define VIRTIO_MSG_EVENT_AVAIL_WRAP_SHIFT	31
	__le32 next_offset_wrap;
} __attribute__((packed));

struct event_used {
	__le32 index;
} __attribute__((packed));

struct virtio_msg {
	#define VIRTIO_MSG_TYPE_REQUEST		(0 << 0)
	#define VIRTIO_MSG_TYPE_RESPONSE	(1 << 0)
	#define VIRTIO_MSG_TYPE_TRANSPORT	(0 << 1)
	#define VIRTIO_MSG_TYPE_BUS		(1 << 1)
	__u8 type;

	__u8 msg_id;
	__le16 dev_id;
	__le16 msg_size;
	__u8 payload[];
} __attribute__((packed));

static inline void *virtio_msg_payload(struct virtio_msg *vmsg)
{
	return &vmsg->payload;
}

/* Virtio message bus definitions */

/* Message types */
#define VIRTIO_MSG_BUS_GET_DEVICES	0x02
#define VIRTIO_MSG_BUS_PING		0x03
#define VIRTIO_MSG_BUS_EVENT_DEVICE	0x40

struct bus_get_devices {
	__le16 offset;
	__le16 num;
} __attribute__((packed));

struct bus_get_devices_resp {
	__le16 offset;
	__le16 num;
	__le16 next_offset;
	__u8 devices[];
} __attribute__((packed));

struct bus_event_device {
	__le16 dev_num;
	#define VIRTIO_MSG_BUS_EVENT_DEV_STATE_READY	0x1
	#define VIRTIO_MSG_BUS_EVENT_DEV_STATE_REMOVED	0x2
	__le16 dev_state;
} __attribute__((packed));

struct bus_ping {
	__le32 data;
} __attribute__((packed));

struct bus_ping_resp {
	__le32 data;
} __attribute__((packed));

#endif /* _LINUX_VIRTIO_MSG_H */
