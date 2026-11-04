/* SPDX-License-Identifier: MIT */
/*
 * SSC message layer: protobuf messages inside QMI TLVs. Field numbers and
 * message ids are protocol facts, listed in docs/RESEARCH.md with sources.
 */
#ifndef SSC_H
#define SSC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SSC_SERVICE 400

#define QMI_REQUEST 0
#define QMI_RESPONSE 2
#define QMI_INDICATION 4
#define QMI_SSC_CONTROL 0x20
#define QMI_SSC_REPORT_SMALL 0x21
#define QMI_SSC_REPORT_LARGE 0x22

#define SSC_SUID_UID 0xABABABABABABABABULL

#define SSC_MSG_GET_ATTRIBUTES 1
#define SSC_MSG_DISABLE 10
#define SSC_MSG_SUID_REQ 512
#define SSC_MSG_ENABLE_CONTINUOUS 513
#define SSC_MSG_ENABLE_ON_CHANGE 514
#define SSC_MSG_ATTRIBUTES 128
#define SSC_MSG_SUID_RESP 768		/* also the enable acknowledgement */
#define SSC_MSG_REPORT 1025

#define SSC_ATTR_NAME 0
#define SSC_ATTR_VENDOR 1
#define SSC_ATTR_TYPE 2
#define SSC_ATTR_AVAILABLE 3
#define SSC_ATTR_RATES 6
#define SSC_ATTR_STREAM_TYPE 16
#define SSC_ATTR_PHYSICAL 21

#define SSC_DELIVERY_WAKEUP 0
#define SSC_DELIVERY_NO_WAKEUP 1 /* value assumed: see docs/RESEARCH.md */

struct ssc_uid {
	uint64_t low, high;
};

/* Build the QMI control request holding one SSC client request. Returns length, or 0. */
size_t ssc_build_control(void *buf, size_t cap, uint16_t txn, struct ssc_uid uid, uint32_t msg_id,
			 unsigned delivery, const void *body, size_t body_len, bool has_body);

size_t ssc_body_suid(void *buf, size_t cap, const char *data_type, bool only_default);
size_t ssc_body_attr(void *buf, size_t cap);
size_t ssc_body_rate(void *buf, size_t cap, float hz);

struct qmi_msg {
	unsigned type;
	uint16_t txn, id;
	const uint8_t *tlvs;
	size_t tlv_len;
};

int qmi_parse(const void *buf, size_t len, struct qmi_msg *m);
/* Value of TLV `type`, or NULL. */
const uint8_t *qmi_tlv(const struct qmi_msg *m, uint8_t type, size_t *len);
/* The QMI result code of a response; negative if absent. */
int qmi_result(const struct qmi_msg *m);
/* The protobuf payload of a report indication (u16 length + bytes in TLV 2). */
const uint8_t *ssc_report_payload(const struct qmi_msg *m, size_t *len);

/* Calls back for every event carried by one report. */
typedef void (*ssc_event_cb)(void *ctx, struct ssc_uid uid, uint32_t msg_id, uint64_t ts,
			     const uint8_t *body, size_t len);
int ssc_parse_report(const void *pb, size_t len, ssc_event_cb cb, void *ctx);

/* SUID response. Returns the number of uids stored (up to max), -1 if malformed. */
int ssc_parse_suid(const uint8_t *b, size_t len, char *type, size_t type_cap, struct ssc_uid *uids,
		   int max);

/* Sample: repeated float (either encoding) in field 1, accuracy in field 2. */
int ssc_parse_sample(const uint8_t *b, size_t len, float *v, int max, int *accuracy);

struct ssc_attr_value {
	enum { ATTR_NONE, ATTR_STR, ATTR_FLOAT, ATTR_INT, ATTR_BOOL, ATTR_ARRAY } kind;
	const char *s;	/* not terminated */
	size_t slen;
	float f;
	uint64_t i;
	bool b;
};
typedef void (*ssc_attr_cb)(void *ctx, int id, const struct ssc_attr_value *v);
int ssc_parse_attrs(const uint8_t *b, size_t len, ssc_attr_cb cb, void *ctx);

#endif
