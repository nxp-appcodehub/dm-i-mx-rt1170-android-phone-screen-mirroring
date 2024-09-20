#ifndef SC_CONTROLMSG_H
#define SC_CONTROLMSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "android/input.h"
#include "android/keycodes.h"
#include "coords.h"

#define SC_CONTROL_MSG_MAX_SIZE (1 << 18) // 256k

enum sc_control_msg_type {
	SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT = 2,
};

struct sc_control_msg {
	enum sc_control_msg_type type;
	union {
		struct {
			enum android_motionevent_action action;
			enum android_motionevent_buttons action_button;
			enum android_motionevent_buttons buttons;
			uint64_t pointer_id;
			struct sc_position position;
			float pressure;
		} inject_touch_event;
	};
};

// buf size must be at least CONTROL_MSG_MAX_SIZE
// return the number of bytes written
size_t sc_control_msg_serialize(const struct sc_control_msg *msg, unsigned char *buf);

#endif