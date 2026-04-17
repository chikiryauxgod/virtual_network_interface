#include <linux/etherdevice.h>
#include <linux/icmp.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <net/ip.h>

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

static void vni_reset_ip_config(void)
{
	mutex_lock(&vni_ip_lock);
	strscpy(vni_ip_addr, VNI_IP_UNSET, sizeof(vni_ip_addr));
	vni_ip_be = 0;
	vni_ip_is_set = false;
	mutex_unlock(&vni_ip_lock);
}

static bool vni_get_configured_ip(__be32 *ip_addr)
{
	bool is_set;

	mutex_lock(&vni_ip_lock);
	is_set = vni_ip_is_set;
	if (is_set)
		*ip_addr = vni_ip_be;
	mutex_unlock(&vni_ip_lock);

	return is_set;
}

static struct sk_buff *vni_build_echo_reply(struct sk_buff *skb,
					    struct net_device *dev,
					    __be32 configured_ip)
{
	struct sk_buff *reply;
	struct ethhdr *eth;
	struct iphdr *iph;
	struct iphdr *reply_iph;
	struct icmphdr *icmp;
	struct icmphdr *reply_icmp;
	unsigned int ip_hdr_len;
	unsigned int icmp_len;
	__be32 addr_tmp;
	unsigned char mac_tmp[ETH_ALEN];

	if (skb->protocol != htons(ETH_P_IP))
		return NULL;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		return NULL;

	iph = ip_hdr(skb);
	if (iph->version != 4 || iph->ihl < 5 || iph->protocol != IPPROTO_ICMP)
		return NULL;

	ip_hdr_len = iph->ihl * 4;
	if (!pskb_may_pull(skb, ip_hdr_len + sizeof(struct icmphdr)))
		return NULL;

	iph = ip_hdr(skb);
	if (iph->daddr != configured_ip)
		return NULL;

	icmp = (struct icmphdr *)((u8 *)iph + ip_hdr_len);
	if (icmp->type != ICMP_ECHO)
		return NULL;

	reply = skb_copy_expand(skb, 0, 0, GFP_ATOMIC);
	if (!reply)
		return NULL;

	if (skb_mac_header_was_set(reply)) {
		eth = eth_hdr(reply);
		ether_addr_copy(mac_tmp, eth->h_source);
		ether_addr_copy(eth->h_source, eth->h_dest);
		ether_addr_copy(eth->h_dest, mac_tmp);
	}

	skb_set_mac_header(reply, skb_mac_offset(skb));
	skb_set_network_header(reply, skb_network_offset(skb));
	skb_set_transport_header(reply, skb_network_offset(skb) + ip_hdr_len);
	reply_iph = ip_hdr(reply);
	reply_icmp = (struct icmphdr *)((u8 *)reply_iph + ip_hdr_len);

	addr_tmp = reply_iph->saddr;
	reply_iph->saddr = reply_iph->daddr;
	reply_iph->daddr = addr_tmp;
	reply_iph->ttl = 64;
	reply_iph->check = 0;
	ip_send_check(reply_iph);

	reply_icmp->type = ICMP_ECHOREPLY;
	reply_icmp->checksum = 0;
	icmp_len = ntohs(reply_iph->tot_len) - ip_hdr_len;
	reply_icmp->checksum = ip_compute_csum((void *)reply_icmp, icmp_len);

	reply->dev = dev;
	reply->protocol = eth_type_trans(reply, dev);
	reply->pkt_type = PACKET_HOST;
	reply->skb_iif = dev->ifindex;
	reply->ip_summed = CHECKSUM_NONE;

	return reply;
}

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
	struct sk_buff *reply = NULL;
	__be32 configured_ip;

	if (vni_get_configured_ip(&configured_ip))
		reply = vni_build_echo_reply(skb, dev, configured_ip);

	dev_kfree_skb(skb);

	if (reply) {
		netif_rx(reply);
		pr_debug("%s: replied to ICMP echo request\n", VNI_MODULE_NAME);
	}

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

	if (count >= sizeof(input))
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

	vni_reset_ip_config();

	vni_dev = alloc_netdev(0, VNI_IFACE_NAME, NET_NAME_UNKNOWN, vni_setup);
	if (!vni_dev) {
		pr_err("%s: failed to allocate net_device\n", VNI_MODULE_NAME);
		return -ENOMEM;
	}

	vni_proc_entry = proc_create(VNI_PROC_ENTRY, 0644, NULL, &vni_proc_ops);
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

	vni_reset_ip_config();

	pr_info("%s: module unloaded\n", VNI_MODULE_NAME);
}

module_init(vni_init);
module_exit(vni_exit);
