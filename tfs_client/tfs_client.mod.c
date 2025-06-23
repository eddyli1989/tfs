#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_MITIGATION_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x92997ed8, "_printk" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0xb5b54b34, "_raw_spin_unlock" },
	{ 0x69188c61, "kmem_cache_alloc" },
	{ 0x2238dbcd, "inode_init_once" },
	{ 0x37a0cba, "kfree" },
	{ 0x3a6031f4, "get_tree_nodev" },
	{ 0x5d3cb2e8, "new_inode" },
	{ 0xe953b21f, "get_next_ino" },
	{ 0x1f2e3d28, "current_time" },
	{ 0xbc208b8a, "set_nlink" },
	{ 0x2c085079, "d_instantiate" },
	{ 0xd9b85ef6, "lockref_get" },
	{ 0x7ea9f9c4, "inc_nlink" },
	{ 0xa65c6def, "alt_cb_patch_nops" },
	{ 0x43b7f0a9, "setattr_prepare" },
	{ 0x92f9a3a1, "inode_newsize_ok" },
	{ 0x987b1f2a, "truncate_setsize" },
	{ 0x761e53f2, "setattr_copy" },
	{ 0xe1537255, "__list_del_entry_valid" },
	{ 0x63833020, "kmem_cache_free" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x587f22d7, "devmap_managed_key" },
	{ 0x6315f648, "__put_devmap_managed_page_refs" },
	{ 0xa3c4430d, "__folio_put" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x6ff3a485, "dynamic_might_resched" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x57bc19d2, "down_write" },
	{ 0xce807a25, "up_write" },
	{ 0x9688de8b, "memstart_addr" },
	{ 0x65b86c3c, "remap_pfn_range" },
	{ 0x4b141b78, "unregister_filesystem" },
	{ 0xd11a0293, "misc_deregister" },
	{ 0xa157c884, "kmem_cache_destroy" },
	{ 0x74513649, "kmem_cache_create" },
	{ 0xd47d42b6, "kmalloc_caches" },
	{ 0x41e2159d, "kmalloc_trace" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x72a73812, "misc_register" },
	{ 0x21b6e4fd, "register_filesystem" },
	{ 0x1306b2f1, "d_make_root" },
	{ 0xdcbe5d85, "iput" },
	{ 0x68f31cbd, "__list_add_valid" },
	{ 0xe2964344, "__wake_up" },
	{ 0xcd5698e7, "get_user_pages_fast" },
	{ 0xf4f5c2a5, "alloc_pages" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0xdcb764ad, "memset" },
	{ 0xf60e701b, "__free_pages" },
	{ 0xb6ea1e2b, "simple_lookup" },
	{ 0x7e3667c2, "simple_link" },
	{ 0x46ae67e9, "simple_unlink" },
	{ 0x9b0010f4, "simple_rmdir" },
	{ 0x99767f8c, "simple_rename" },
	{ 0x943d5c9a, "generic_file_llseek" },
	{ 0xb87c8942, "generic_read_dir" },
	{ 0xc2cb9b26, "dcache_dir_open" },
	{ 0x1a369b76, "dcache_dir_close" },
	{ 0x3ddfc132, "noop_fsync" },
	{ 0xc2384316, "generic_file_mmap" },
	{ 0x8bcab1e2, "generic_file_open" },
	{ 0x7f85ad39, "kill_anon_super" },
	{ 0x56710775, "generic_delete_inode" },
	{ 0xf9f31f40, "param_ops_bool" },
	{ 0x8d81f960, "param_ops_uint" },
	{ 0x6e5dbd7f, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "BA547E45C63855EDF0C0EF2");
MODULE_INFO(rhelversion, "9.6");
