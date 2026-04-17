#include <linux/etherdevice.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "../include/vni.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lew");
MODULE_DESCRIPTION("Virtual network interface kernel module");

static struct net_device *vni_dev;
static struct proc_dir_entry *vni_proc_entry;
static DEFINE_MUTEX(vni_ip_lock);
static char vni_ip_addr[VNI_IP_ADDR_LEN] = VNI_IP_UNSET;
static __be32 vni_ip_be;
static bool vni_ip_is_set;

static int vni_open(struct net_device *dev)
{
	netif_start_queue(dev);
	pr_info("%s: interface %s opened\n", VNI_MODULE_NAME, dev->name);
	return 0;
}

static int vni_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	pr_info("%s: interface %s stopped\n", VNI_MODULE_NAME, dev->name);
	return 0;
}

static netdev_tx_t vni_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops vni_netdev_ops = {
	.ndo_open = vni_open,
	.ndo_stop = vni_stop,
	.ndo_start_xmit = vni_start_xmit,
};

static void vni_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &vni_netdev_ops;
	dev->flags |= IFF_NOARP;
	dev->mtu = ETH_DATA_LEN;
	eth_hw_addr_random(dev);
}

static ssize_t vni_proc_read(struct file *file, char __user *buf, size_t count,
			     loff_t *ppos)
{
	char output[32];
	int len;

	mutex_lock(&vni_ip_lock);
	len = scnprintf(output, sizeof(output), "ip=%s\n", vni_ip_addr);
	mutex_unlock(&vni_ip_lock);

	return simple_read_from_buffer(buf, count, ppos, output, len);
}

static ssize_t vni_proc_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	char input[VNI_IP_ADDR_LEN];
	char *ip_str;
	u8 addr[4];
	__be32 ip_be;
	size_t len;

	if (count == 0)
		return -EINVAL;

	len = min(count, sizeof(input) - 1);
	if (copy_from_user(input, buf, len))
		return -EFAULT;

	input[len] = '\0';
	ip_str = strim(input);
	if (*ip_str == '\0')
		return -EINVAL;

	if (!in4_pton(ip_str, -1, addr, '\0', NULL))
		return -EINVAL;

	ip_be = cpu_to_be32((addr[0] << 24) | (addr[1] << 16) |
			    (addr[2] << 8) | addr[3]);

	mutex_lock(&vni_ip_lock);
	strscpy(vni_ip_addr, ip_str, sizeof(vni_ip_addr));
	vni_ip_be = ip_be;
	vni_ip_is_set = true;
	mutex_unlock(&vni_ip_lock);

	pr_info("%s: configured IPv4 address %s via /proc/%s\n",
		VNI_MODULE_NAME, ip_str, VNI_PROC_ENTRY);

	return count;
}

static const struct proc_ops vni_proc_ops = {
	.proc_read = vni_proc_read,
	.proc_write = vni_proc_write,
};

static int __init vni_init(void)
{
	int ret;

	mutex_lock(&vni_ip_lock);
	strscpy(vni_ip_addr, VNI_IP_UNSET, sizeof(vni_ip_addr));
	vni_ip_be = 0;
	vni_ip_is_set = false;
	mutex_unlock(&vni_ip_lock);

	vni_dev = alloc_netdev(0, VNI_IFACE_NAME, NET_NAME_UNKNOWN, vni_setup);
	if (!vni_dev) {
		pr_err("%s: failed to allocate net_device\n", VNI_MODULE_NAME);
		return -ENOMEM;
	}

	vni_proc_entry = proc_create(VNI_PROC_ENTRY, 0666, NULL, &vni_proc_ops);
	if (!vni_proc_entry) {
		pr_err("%s: failed to create /proc/%s\n",
		       VNI_MODULE_NAME, VNI_PROC_ENTRY);
		free_netdev(vni_dev);
		vni_dev = NULL;
		return -ENOMEM;
	}

	ret = register_netdev(vni_dev);
	if (ret) {
		pr_err("%s: failed to register interface: %d\n",
		       VNI_MODULE_NAME, ret);
		proc_remove(vni_proc_entry);
		vni_proc_entry = NULL;
		free_netdev(vni_dev);
		vni_dev = NULL;
		return ret;
	}

	pr_info("%s: created /proc/%s\n", VNI_MODULE_NAME, VNI_PROC_ENTRY);
	pr_info("%s: module loaded, interface %s registered\n",
		VNI_MODULE_NAME, vni_dev->name);
	return 0;
}

static void __exit vni_exit(void)
{
	if (vni_dev) {
		unregister_netdev(vni_dev);
		free_netdev(vni_dev);
		vni_dev = NULL;
	}

	if (vni_proc_entry) {
		proc_remove(vni_proc_entry);
		vni_proc_entry = NULL;
	}

	pr_info("%s: module unloaded\n", VNI_MODULE_NAME);
}

module_init(vni_init);
module_exit(vni_exit);
