#include <linux/delay.h>
#include <linux/module.h>
#define DEBUG
#include <linux/device.h>

#include "rcio.h"
#include "protocol.h"

#define rcio_status_err(__dev, format, args...)\
        dev_err(__dev, "rcio_status: " format, ##args)
#define rcio_status_warn(__dev, format, args...)\
        dev_warn(__dev, "rcio_status: " format, ##args)

#define RCIO_ADC_CHANNELS_COUNT 6

static void handle_status(uint16_t status);
static void handle_alarms(uint16_t alarms);
static bool rcio_status_request_crc(struct rcio_state *state);
static bool rcio_status_request_board_type(struct rcio_state *state);
static bool rcio_status_request_git_hash(struct rcio_state *state);

char *board_names[] =
{
	[NAVIO2] = "navio2",
	[EDGE] = "edge",
	[NEW_BOARD1] = "new_board1",
	[NEW_BOARD2] = "new_board2",
	[UNKNOWN_BOARD] = "unknown board",
};

static struct rcio_status {
    unsigned long timeout;
    unsigned long crc;
    bool init_ok;
    bool pwm_ok;
    bool alive;
    board_type_t board_type;
    char git_hash[20];
    struct rcio_state *rcio;
} status;

bool rcio_status_update(struct rcio_state *state);

static ssize_t init_ok_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", status.init_ok? 1: 0);
}

static ssize_t pwm_ok_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", status.pwm_ok? 1: 0);
}

static ssize_t alive_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", status.alive? 1: 0);
}

static ssize_t crc_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "0x%lx\n", status.crc);
}

static ssize_t board_name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n", board_names[status.rcio->board_type]);
}


static struct kobj_attribute init_ok_attribute = __ATTR(init_ok, S_IRUGO, init_ok_show, NULL);
static struct kobj_attribute pwm_ok_attribute = __ATTR(pwm_ok, S_IRUGO, pwm_ok_show, NULL);
static struct kobj_attribute alive_attribute = __ATTR(alive, S_IRUGO, alive_show, NULL);
static struct kobj_attribute crc_attribute = __ATTR(crc, S_IRUGO, crc_show, NULL);
static struct kobj_attribute board_name_attribute = __ATTR(board_name, S_IRUGO, board_name_show, NULL);

static struct attribute *attrs[] = {
    &init_ok_attribute.attr,
    &pwm_ok_attribute.attr,
    &alive_attribute.attr,
    &crc_attribute.attr, 
    &board_name_attribute.attr,
    NULL,
};

static struct attribute_group attr_group = {
    .name = "status",
    .attrs = attrs,
};

bool rcio_status_update(struct rcio_state *state)
{
    uint16_t regs[6];

    if (time_before(jiffies, status.timeout)) {
        return false;
    }

    if (state->register_get(state, PX4IO_PAGE_STATUS, PX4IO_P_STATUS_FLAGS, regs, ARRAY_SIZE(regs)) < 0) {
        status.alive = false;
        return false;
    }

    if (!rcio_status_request_crc(state)) {
        rcio_status_err(state->adapter->dev, "Could not update CRC\n");
    } 

    status.alive = true;

    handle_status(regs[0]);
    handle_alarms(regs[1]);

    status.timeout = jiffies + HZ / 5; /* timeout in 0.2s */
    return true;
}

bool rcio_status_probe(struct rcio_state *state)
{
    int ret;

    status.rcio = state;

    status.timeout = jiffies + HZ / 50; /* timeout in 0.02s */

    ret = sysfs_create_group(status.rcio->object, &attr_group);

    if (ret < 0) {
        rcio_status_err(state->adapter->dev, "module not registered int sysfs\n");
        return false;
    }

    status.init_ok = false;

    if (!rcio_status_request_crc(state)) {
        rcio_status_err(state->adapter->dev, "could not read CRC\n");
    } else {
        rcio_status_warn(state->adapter->dev, "Firmware CRC: 0x%lx\n", status.crc);
    }
    
	if (!rcio_status_request_board_type(state)) {
        rcio_status_err(state->adapter->dev, "Could not read board type\n");
    } else {
        rcio_status_warn(state->adapter->dev, "Board type: 0x%x (%s)\n", (int)status.rcio->board_type, board_names[status.rcio->board_type]);
    }
    
	if (!rcio_status_request_git_hash(state)) {
        rcio_status_err(state->adapter->dev, "Could not read git hash\n");
    } else {
        rcio_status_warn(state->adapter->dev, "Git hash: %s", status.git_hash);
    }

    return true;
}

static bool rcio_status_request_crc(struct rcio_state *state) {
    uint16_t regs[2];

    if (state->register_get(state, PX4IO_PAGE_SETUP, PX4IO_P_SETUP_CRC, regs, ARRAY_SIZE(regs)) < 0) {
        return false;
    }

    status.crc = regs[1] << 16 | regs[0];
    return true;
}

static bool rcio_status_request_board_type(struct rcio_state *state) {
    uint16_t reg;

    if (state->register_get(state, PX4IO_PAGE_STATUS, PX4IO_P_STATUS_BOARD_TYPE, &reg, 1) < 0) {
        return false;
    }
    
    state->board_type = reg;
    return true;
}

static bool rcio_status_request_git_hash(struct rcio_state *state) {
    if (state->register_get(state, PX4IO_PAGE_GIT_HASH, 0, (uint16_t*)(status.git_hash), 5) < 0) {
        return false;
    }
    return true;
}


static void handle_status(uint16_t st)
{
    if (st & PX4IO_P_STATUS_FLAGS_INIT_OK) {
        status.init_ok = true;
    } else {
        status.init_ok = false;
    }
}

static void handle_alarms(uint16_t alarms)
{
    if (alarms & PX4IO_P_STATUS_ALARMS_PWM_ERROR) {
        status.pwm_ok = false;
    } else {
        status.pwm_ok = true;
    }
}

EXPORT_SYMBOL_GPL(rcio_status_probe);
EXPORT_SYMBOL_GPL(rcio_status_update);
MODULE_AUTHOR("Georgii Staroselskii <georgii.staroselskii@emlid.com>");
MODULE_DESCRIPTION("RCIO status driver");
MODULE_LICENSE("GPL v2");
