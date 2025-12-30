#include "control_msg.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "util/binary.h"

static void write_position(uint8_t *buf, const struct sc_position *position)
{
	sc_write32be(&buf[0], position->point.x);
	sc_write32be(&buf[4], position->point.y);
	sc_write16be(&buf[8], position->screen_size.width);
	sc_write16be(&buf[10], position->screen_size.height);
}

size_t sc_control_msg_serialize(const struct sc_control_msg *msg, unsigned char *buf)
{
	buf[0] = msg->type;
	switch (msg->type) {
	case SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT:
		buf[1] = msg->inject_touch_event.action;
		sc_write64be(&buf[2], msg->inject_touch_event.pointer_id);
		write_position(&buf[10], &msg->inject_touch_event.position);
		uint16_t pressure = sc_float_to_u16fp(msg->inject_touch_event.pressure);
		sc_write16be(&buf[22], pressure);
		sc_write32be(&buf[24], msg->inject_touch_event.action_button);
		sc_write32be(&buf[28], msg->inject_touch_event.buttons);
		return 32;
	case SC_CONTROL_MSG_TYPE_DUMMY:
		return 1;
	default:
		return 0;
	}
}