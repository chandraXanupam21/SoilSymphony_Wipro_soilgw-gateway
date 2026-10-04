/* Shared kernel/user protocol definition for the Soil-Moisture Gateway.
 *
 * Wire frame (8 bytes):
 *   [0] SOF = 0xAA
 *   [1] node id (1..32)
 *   [2] sequence number (wraps at 255)
 *   [3] moisture high byte  \ unsigned 16-bit, big endian, units of 0.01 %
 *   [4] moisture low byte   /
 *   [5] battery mV high byte \ unsigned 16-bit, big endian
 *   [6] battery mV low byte  /
 *   [7] CRC-8 (poly 0x07, init 0) over bytes [1..6]
 */
#ifndef SOILGW_PROTO_H
#define SOILGW_PROTO_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define SOILGW_SOF        0xAA
#define SOILGW_FRAME_LEN  8
#define SOILGW_MAX_NODES  32
#define SOILGW_DEV_NAME   "soilgw"

struct soilgw_stats {
	__u32 frames_ok;       /* valid frames accepted into ring buffer   */
	__u32 frames_bad;      /* CRC / node-id failures                   */
	__u32 frames_filtered; /* valid but rejected by node mask          */
	__u32 overruns;        /* ring full, oldest frame dropped          */
	__u32 resyncs;         /* garbage bytes skipped while hunting SOF  */
	__u32 bytes_in;        /* total bytes written to the device        */
	__u32 queued;          /* frames currently waiting in ring buffer  */
};

#define SOILGW_IOC_MAGIC     'S'
#define SOILGW_IOC_GET_STATS _IOR(SOILGW_IOC_MAGIC, 1, struct soilgw_stats)
#define SOILGW_IOC_CLEAR     _IO(SOILGW_IOC_MAGIC, 2)
#define SOILGW_IOC_SET_MASK  _IOW(SOILGW_IOC_MAGIC, 3, __u32)
#define SOILGW_IOC_GET_MASK  _IOR(SOILGW_IOC_MAGIC, 4, __u32)

static inline __u8 soilgw_crc8(const __u8 *d, unsigned int n)
{
	__u8 crc = 0;
	unsigned int i;
	int b;

	for (i = 0; i < n; i++) {
		crc ^= d[i];
		for (b = 0; b < 8; b++)
			crc = (crc & 0x80) ? (__u8)((crc << 1) ^ 0x07)
					   : (__u8)(crc << 1);
	}
	return crc;
}

#endif /* SOILGW_PROTO_H */
