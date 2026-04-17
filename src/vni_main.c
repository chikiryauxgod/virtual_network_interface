#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include "../include/vni.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lew");
MODULE_DESCRIPTION("Virtual network interface kernel module");

static struct net_device *vni_dev;

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

static int __init vni_init(void)
{
	int ret;

	vni_dev = alloc_netdev(0, VNI_IFACE_NAME, NET_NAME_UNKNOWN, vni_setup);
	if (!vni_dev)
		return -ENOMEM;

	ret = register_netdev(vni_dev);
	if (ret) {
		free_netdev(vni_dev);
		vni_dev = NULL;
		return ret;
	}

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

	pr_info("%s: module unloaded\n", VNI_MODULE_NAME);
}

module_init(vni_init);
module_exit(vni_exit);
