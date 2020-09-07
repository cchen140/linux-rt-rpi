#include <linux/module.h>

#include "rcio.h"
#include "protocol.h"
#include "rcio_rcin_priv.h"

#define RCIO_RCIN_MAX_CHANNELS 16

static struct rcio_state *rcio;

static int rcin_get_raw_values(struct rcio_state *state, struct rc_input_values *rc_val);

static u16 measurements[RCIO_RCIN_MAX_CHANNELS] = {0};

static ssize_t channel_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int value = -1;

    int channel;

    if (sscanf(attr->attr.name, "ch%d", &channel) < 0) {
        return -EIO;
    }

    value = measurements[channel];

    if (value < 0) {
        return value;
    }

    return sprintf(buf, "%d\n", value);
}

static bool connected; 
static ssize_t connected_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", connected? 1: 0);
}

#define RCIN_CHANNEL_ATTR(channel) __ATTR(channel, S_IRUGO, channel_show, NULL)

static struct kobj_attribute ch0_attribute = RCIN_CHANNEL_ATTR(ch0);
static struct kobj_attribute ch1_attribute = RCIN_CHANNEL_ATTR(ch1);
static struct kobj_attribute ch2_attribute = RCIN_CHANNEL_ATTR(ch2);
static struct kobj_attribute ch3_attribute = RCIN_CHANNEL_ATTR(ch3);
static struct kobj_attribute ch4_attribute = RCIN_CHANNEL_ATTR(ch4);
static struct kobj_attribute ch5_attribute = RCIN_CHANNEL_ATTR(ch5);
static struct kobj_attribute ch6_attribute = RCIN_CHANNEL_ATTR(ch6);
static struct kobj_attribute ch7_attribute = RCIN_CHANNEL_ATTR(ch7);
static struct kobj_attribute ch8_attribute = RCIN_CHANNEL_ATTR(ch8);
static struct kobj_attribute ch9_attribute = RCIN_CHANNEL_ATTR(ch9);
static struct kobj_attribute ch10_attribute = RCIN_CHANNEL_ATTR(ch10);
static struct kobj_attribute ch11_attribute = RCIN_CHANNEL_ATTR(ch11);
static struct kobj_attribute ch12_attribute = RCIN_CHANNEL_ATTR(ch12);
static struct kobj_attribute ch13_attribute = RCIN_CHANNEL_ATTR(ch13);
static struct kobj_attribute ch14_attribute = RCIN_CHANNEL_ATTR(ch14);
static struct kobj_attribute ch15_attribute = RCIN_CHANNEL_ATTR(ch15);

static struct kobj_attribute connected_attribute = __ATTR_RO(connected);

static struct attribute *attrs[] = {
    &ch0_attribute.attr,
    &ch1_attribute.attr,
    &ch2_attribute.attr,
    &ch3_attribute.attr,
    &ch4_attribute.attr,
    &ch5_attribute.attr,
    &ch6_attribute.attr,
    &ch7_attribute.attr,
    &ch8_attribute.attr,
    &ch9_attribute.attr,
    &ch10_attribute.attr,
    &ch11_attribute.attr,
    &ch12_attribute.attr,
    &ch13_attribute.attr,
    &ch14_attribute.attr,
    &ch15_attribute.attr,
    &connected_attribute.attr,
    NULL,
};

static struct attribute_group attr_group = {
    .name = "rcin",
    .attrs = attrs,
};

static unsigned long timeout;

bool rcio_rcin_update(struct rcio_state *state)
{
    int ret;
    struct rc_input_values report;

    if (time_before(jiffies, timeout)) {
        return false;
    }

    ret = rcin_get_raw_values(state, &report);

    if (ret == -ENOTCONN) {
        connected = false;
        for (int i = 0; i < RCIO_RCIN_MAX_CHANNELS; i++) {
            measurements[i] = 0;
        }
        return true;
    } else if (ret < 0) {
        connected = false;
        return false;
    }

    connected = true;

    for (int i = 0; i < RCIO_RCIN_MAX_CHANNELS; i++) {
        if (report.values[i] > 2500 || report.values[i] < 800) {
           continue; 
        }

        measurements[i] = report.values[i];
    }
    
    timeout = jiffies + HZ / 100; /* timeout in 0.01s */

    return true;
}

int rcio_rcin_probe(struct rcio_state *state)
{
    int ret;

    rcio = state;

    timeout = jiffies + HZ / 100; /* timeout in 0.01s */

    ret = sysfs_create_group(rcio->object, &attr_group);

    if (ret < 0) {
        printk(KERN_INFO "sysfs failed\n");
    }

    connected = false;

    return 0;
}

static int rcin_get_raw_values(struct rcio_state *state, struct rc_input_values *rc_val)
{
    uint16_t status;
    int ret;

    if ((ret = state->register_get(state, PX4IO_PAGE_STATUS, PX4IO_P_STATUS_FLAGS, &status, 1)) < 0) {
        return ret;
    }

    /* if no R/C input, don't try to fetch anything */
    if (!(status & PX4IO_P_STATUS_FLAGS_RC_OK)) {
        return -ENOTCONN;
    }

    /* sort out the source of the values */
    if (status & PX4IO_P_STATUS_FLAGS_RC_PPM) {
        rc_val->input_source = RC_INPUT_SOURCE_PX4IO_PPM;

    } else if (status & PX4IO_P_STATUS_FLAGS_RC_DSM) {
        rc_val->input_source = RC_INPUT_SOURCE_PX4IO_SPEKTRUM;

    } else if (status & PX4IO_P_STATUS_FLAGS_RC_SBUS) {
        rc_val->input_source = RC_INPUT_SOURCE_PX4IO_SBUS;

    } else if (status & PX4IO_P_STATUS_FLAGS_RC_ST24) {
        rc_val->input_source = RC_INPUT_SOURCE_PX4IO_ST24;

    } else {
        rc_val->input_source = RC_INPUT_SOURCE_UNKNOWN;
    }

    /* read raw R/C input values */
    if (state->register_get(state, PX4IO_PAGE_RAW_RC_INPUT, PX4IO_P_RAW_RC_BASE, 
                &(rc_val->values[0]), RCIO_RCIN_MAX_CHANNELS) < 0) {
        return -EIO;
    }
    
    return 0;
}

EXPORT_SYMBOL_GPL(rcio_rcin_probe);
EXPORT_SYMBOL_GPL(rcio_rcin_update);

MODULE_AUTHOR("Georgii Staroselskii <georgii.staroselskii@emlid.com>");
MODULE_DESCRIPTION("RCIO RC Input driver");
MODULE_LICENSE("GPL v2");
