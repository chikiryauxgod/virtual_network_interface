#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "../include/vni.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lew");
MODULE_DESCRIPTION("Virtual network interface kernel module");

static int __init vni_init(void)
{
	pr_info("%s: module loaded\n", VNI_MODULE_NAME);
	return 0;
}

static void __exit vni_exit(void)
{
	pr_info("%s: module unloaded\n", VNI_MODULE_NAME);
}

module_init(vni_init);
module_exit(vni_exit);
