# Virtual Network Interface Kernel Module

`vni` is a Linux kernel module that creates a virtual network interface `vni0`.
The module exposes `/proc/vni`, accepts an IPv4 address written there, and replies to ICMP echo requests for that address.

The project is intentionally minimal. It demonstrates a small virtual `net_device`, simple control through `procfs`, and a basic ICMP echo reply path inside the kernel.

## Usage

Build the module:

```bash
make clean
make
```

Load the module:

```bash
sudo insmod vni.ko
```

Configure the interface and IPv4 address:

```bash
sudo ip link set vni0 up
echo "192.168.50.1" | sudo tee /proc/vni
sudo ip route add 192.168.50.1/32 dev vni0
```

Check the configured address:

```bash
cat /proc/vni
```

Test ICMP echo reply:

```bash
ping -c 3 192.168.50.1
```

Unload the module:

```bash
sudo ip route del 192.168.50.1/32 dev vni0
sudo rmmod vni
```

## Quick Test

Run the smoke test:

```bash
sudo ./tests/test.sh
```
