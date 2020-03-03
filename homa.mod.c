#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0x50a74865, "module_layout" },
	{ 0x8036ad3f, "release_sock" },
	{ 0xfc63c784, "kmalloc_caches" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0xc57c6d80, "unregister_net_sysctl_table" },
	{ 0xa8181adf, "proc_dointvec" },
	{ 0xa0c6befa, "hrtimer_cancel" },
	{ 0x47939e0d, "__tasklet_hi_schedule" },
	{ 0x17b85ae1, "dst_release" },
	{ 0xb3635b01, "_raw_spin_lock_bh" },
	{ 0x56470118, "__warn_printk" },
	{ 0x2e0eae65, "inet_sendmsg" },
	{ 0x56c23cbe, "sk_set_peek_off" },
	{ 0x409bcb62, "mutex_unlock" },
	{ 0x999e8297, "vfree" },
	{ 0x130f4852, "inet_del_protocol" },
	{ 0x165b145c, "ex_handler_refcount" },
	{ 0x7a2af7b4, "cpu_number" },
	{ 0xea958490, "pv_ops" },
	{ 0x7a7b2bd5, "sk_common_release" },
	{ 0x68273e4a, "kthread_create_on_node" },
	{ 0x15ba50a6, "jiffies" },
	{ 0xd99ef5b8, "proc_remove" },
	{ 0x5dc5dee7, "inet_dgram_connect" },
	{ 0x454b0e9b, "sock_no_sendpage" },
	{ 0x704e9489, "__pskb_pull_tail" },
	{ 0xdb08516a, "sock_no_mmap" },
	{ 0xb44ad4b3, "_copy_to_user" },
	{ 0x26aada56, "ip4_datagram_connect" },
	{ 0xfbdfc558, "hrtimer_start_range_ns" },
	{ 0xfb578fc5, "memset" },
	{ 0x8d01271f, "sock_no_socketpair" },
	{ 0x963dcba1, "_raw_spin_trylock_bh" },
	{ 0xf905b5de, "current_task" },
	{ 0x51eaf931, "skb_copy_datagram_iter" },
	{ 0xf0875158, "security_sk_classify_flow" },
	{ 0x977f511b, "__mutex_init" },
	{ 0xc5850110, "printk" },
	{ 0x1973a989, "kthread_stop" },
	{ 0x61c15a24, "lock_sock_nested" },
	{ 0xd532f06b, "sock_no_listen" },
	{ 0xccd62b4d, "__ip_queue_xmit" },
	{ 0x2ab7989d, "mutex_lock" },
	{ 0x3f82283a, "inet_del_offload" },
	{ 0x624ca6, "sock_no_accept" },
	{ 0x14a20824, "inet_add_protocol" },
	{ 0x9545af6d, "tasklet_init" },
	{ 0x303bb077, "inet_add_offload" },
	{ 0x44da4c0c, "init_net" },
	{ 0x952664c5, "do_exit" },
	{ 0x972ace60, "inet_ioctl" },
	{ 0x82072614, "tasklet_kill" },
	{ 0x5820a579, "proto_register" },
	{ 0x7448aa09, "inet_release" },
	{ 0x1eeecbd6, "__alloc_skb" },
	{ 0xd6b33026, "cpu_khz" },
	{ 0x49c41a57, "_raw_spin_unlock_bh" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x1000e51, "schedule" },
	{ 0xb8b9f817, "kmalloc_order_trace" },
	{ 0xc1e7fb7, "kfree_skb" },
	{ 0x5afc57e5, "proto_unregister" },
	{ 0x25cbbe50, "inet_getname" },
	{ 0x2ea2c95c, "__x86_indirect_thunk_rax" },
	{ 0x836431c7, "wake_up_process" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x4114b18d, "kmem_cache_alloc_trace" },
	{ 0xdbf17652, "_raw_spin_lock" },
	{ 0xb8a9cdb2, "ip_route_output_flow" },
	{ 0x9ea53d7f, "vsnprintf" },
	{ 0x69a610b6, "sock_common_setsockopt" },
	{ 0x683230da, "inet_register_protosw" },
	{ 0x37a0cba, "kfree" },
	{ 0x69acdf38, "memcpy" },
	{ 0x1ee7d3cd, "hrtimer_init" },
	{ 0x264b82aa, "sock_common_getsockopt" },
	{ 0xae26bc9b, "skb_dequeue" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xbd9debeb, "import_single_range" },
	{ 0x70122883, "proc_create" },
	{ 0xcc36a582, "register_net_sysctl" },
	{ 0x3cbdc3c2, "skb_put" },
	{ 0x6562cf7, "ip4_datagram_release_cb" },
	{ 0x362ef408, "_copy_from_user" },
	{ 0x5d093c09, "inet_unregister_protosw" },
	{ 0xd614cd7c, "inet_recvmsg" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "DBF108E73C3DEBE8A1492C2");
