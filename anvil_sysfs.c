#ifdef DEBUG
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include "anvil_sysfs.h"

/* Pulling variables from anvil */
extern unsigned long refresh_count;
extern unsigned long L1_count;
extern unsigned long L2_count;

static struct kobject *anvil_kobj;

static ssize_t refresh_count_show(struct kobject *kobj,
                                  struct kobj_attribute *attr,
                                  char *buf)
{
    return sprintf(buf, "%lu\n", refresh_count);
}

static ssize_t L1_count_show(struct kobject *kobj,
                             struct kobj_attribute *attr,
                             char *buf)
{
    return sprintf(buf, "%lu\n", L1_count);
}

static ssize_t L2_count_show(struct kobject *kobj,
                             struct kobj_attribute *attr,
                             char *buf)
{
    return sprintf(buf, "%lu\n", L2_count);
}

static struct kobj_attribute refresh_count_attr = __ATTR(refresh_count, 0444, refresh_count_show, NULL);
static struct kobj_attribute L1_count_attr = __ATTR(L1_count, 0444, L1_count_show, NULL);
static struct kobj_attribute L2_count_attr = __ATTR(L2_count, 0444, L2_count_show, NULL);

static struct attribute *anvil_attrs[] = {
    &refresh_count_attr.attr,
    &L1_count_attr.attr,
    &L2_count_attr.attr,
    NULL,
};

static struct attribute_group anvil_attr_group = {
    .attrs = anvil_attrs,
};

int anvil_sysfs_init(void)
{
    int ret;
    anvil_kobj = kobject_create_and_add("anvil", kernel_kobj);
    if (!anvil_kobj)
        return -ENOMEM;

    ret = sysfs_create_group(anvil_kobj, &anvil_attr_group);
    if (ret)
        kobject_put(anvil_kobj);

    return ret;
}

void anvil_sysfs_exit(void)
{
    if (anvil_kobj)
        kobject_put(anvil_kobj);
}
#endif