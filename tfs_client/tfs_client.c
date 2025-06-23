#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/pagemap.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/version.h>
#include <linux/backing-dev.h>
#include <linux/timekeeping.h>
#include <linux/statfs.h>  // 用于kstatfs结构体

#define TFS_I(inode) container_of(inode, struct tfs_inode_info, vfs_inode)

// 声明全局变量
static bool enable_zero_copy = true;

#define TFS_DEV_NAME "tfs_client"
#define TFS_MAGIC 0x74667379  // "tfs" in hex
#define MAX_QUEUE_SIZE 128
#define PAGE_SHIFT_4K (12) // 4K page size

// 定义IOCTL命令
#define TFS_MAGIC_IOCTL 'T'
#define TFS_GET_XFER_COUNT _IOR(TFS_MAGIC_IOCTL, 0, int)
#define TFS_GET_XFER_INFO _IOWR(TFS_MAGIC_IOCTL, 1, struct tfs_xfer_info)
#define TFS_RELEASE_XFER _IO(TFS_MAGIC_IOCTL, 2)

// 添加详细的调试宏
#define TFS_DEBUG 1

#ifdef TFS_DEBUG
#define tfs_debug(fmt, ...) printk(KERN_DEBUG "TFS DEBUG: " fmt, ##__VA_ARGS__)
#else
#define tfs_debug(fmt, ...)
#endif

#define tfs_info(fmt, ...) printk(KERN_INFO "TFS INFO: " fmt, ##__VA_ARGS__)
#define tfs_warn(fmt, ...) printk(KERN_WARNING "TFS WARN: " fmt, ##__VA_ARGS__)
#define tfs_error(fmt, ...) printk(KERN_ERR "TFS ERROR: " fmt, ##__VA_ARGS__)

// 传输数据结构
struct tfs_xfer {
    struct page *page;           // 物理页
    off_t offset;                // 文件偏移
    size_t size;                 // 数据大小
    unsigned long pfn;           // 物理页帧号
    struct list_head list;       // 链表节点
};

// IOCTL信息结构体
struct tfs_xfer_info {
    off_t offset;                // 文件偏移
    size_t size;                 // 数据大小
    unsigned long pfn;           // 物理页帧号 (仅用于调试)
};

// 全局上下文结构
struct tfs_data {
    wait_queue_head_t wq;        // 等待队列
    struct list_head xfer_list;  // 传输队列
    spinlock_t lock;             // 队列锁
    struct miscdevice mdev;      // 杂项设备
    struct mutex mmap_lock;      // mmap锁
    struct tfs_xfer *current_xfer; // 当前映射的传输项
    
    // 错误统计
    atomic_t read_errors;
    atomic_t write_errors;
    atomic_t ioctl_errors;
    atomic_t mmap_errors;
};

// 文件系统特定数据结构
struct tfs_fs_info {
    struct backing_dev_info bdi;
};

// 文件系统inode结构
struct tfs_inode_info {
    struct inode vfs_inode;
};

static struct tfs_data *tfs_ctx;
static struct kmem_cache *tfs_inode_cachep;

// 文件系统相关操作
static __used struct inode *tfs_alloc_inode(struct super_block *sb)
{
    struct tfs_inode_info *fsi;
    
    if (!tfs_inode_cachep) {
        tfs_error("inode cache not initialized\n");
        return NULL;
    }
    
    fsi = kmem_cache_alloc(tfs_inode_cachep, GFP_KERNEL);
    if (!fsi) {
        tfs_error("Failed to allocate inode from cache\n");
        return NULL;
    }
    
    tfs_debug("alloc_inode called\n");
    inode_init_once(&fsi->vfs_inode);
    return &fsi->vfs_inode;
}

static void tfs_free_inode(struct inode *inode)
{
    struct tfs_inode_info *fsi;
    
    if (!inode) {
        tfs_error("Attempt to free NULL inode\n");
        return;
    }
    
    tfs_debug("free_inode called\n");
    fsi = container_of(inode, struct tfs_inode_info, vfs_inode);
    kmem_cache_free(tfs_inode_cachep, fsi);
}

// 核心写入函数 - 真正的零拷贝
static ssize_t tfs_file_write(struct file *file, const char __user *ubuf,
                             size_t count, loff_t *ppos)
{
    struct tfs_xfer *xfer;
    struct page *page = NULL;
    unsigned long offset;
    int ret;
    
    tfs_debug("tfs_file_write called: count=%zu, pos=%lld\n", count, *ppos);
    
    if (!count) {
        // 空文件处理 - 创建特殊传输项
        tfs_debug("Empty file write detected, creating special notification\n");
        tfs_info("Processing empty file write request at offset %lld\n", *ppos);
        
        xfer = kzalloc(sizeof(*xfer), GFP_KERNEL);
        if (!xfer) {
            tfs_error("Failed to allocate tfs_xfer for empty file\n");
            return -ENOMEM;
        }
        
        tfs_debug("Successfully allocated transfer structure for empty file\n");
        
        // 设置为空文件标记
        xfer->page = NULL;  // 没有实际页面
        xfer->size = 0;     // 大小为0
        xfer->offset = *ppos;
        xfer->pfn = 0;      // 没有物理页帧
        INIT_LIST_HEAD(&xfer->list);
        
        // 加入传输队列
        spin_lock(&tfs_ctx->lock);
        list_add_tail(&xfer->list, &tfs_ctx->xfer_list);
        tfs_debug("Added empty file transfer to queue\n");
        
        // 获取队列中的传输项数量，用于调试
        int queue_count = 0;
        struct tfs_xfer *tmp;
        list_for_each_entry(tmp, &tfs_ctx->xfer_list, list) {
            queue_count++;
        }
        spin_unlock(&tfs_ctx->lock);
        
        tfs_debug("Current queue size: %d\n", queue_count);
        
        // 唤醒用户态守护进程
        wake_up_interruptible(&tfs_ctx->wq);
        
        tfs_info("Empty file transfer item created and queued successfully\n");
        tfs_debug("Returning success for empty file write\n");
        return 0;
    }

    // 检查用户缓冲区是否有效
    if (!access_ok(ubuf, count)) {
        tfs_error("Invalid user buffer\n");
        return -EFAULT;
    }

    // 检查是否超过页面大小
    if (count > PAGE_SIZE) {
        count = PAGE_SIZE;
    }

    // 获取页面内偏移
    offset = (unsigned long)ubuf & ~PAGE_MASK;
    if (offset + count > PAGE_SIZE) {
        count = PAGE_SIZE - offset;
    }

    // 分配传输结构
    xfer = kzalloc(sizeof(*xfer), GFP_KERNEL);
    if (!xfer) {
        tfs_error("Failed to allocate tfs_xfer\n");
        return -ENOMEM;
    }
    
    // 根据参数选择传输方式
    if (enable_zero_copy) {
        // 零拷贝模式 - 固定用户空间页面
        ret = get_user_pages_fast((unsigned long)ubuf & PAGE_MASK, 1, 1, &page);
        tfs_debug("Using zero-copy transfer mode\n");
    } else {
        // 回退到传统拷贝模式
        page = alloc_page(GFP_KERNEL);
        if (!page) {
            tfs_error("Failed to allocate page for copy mode\n");
            kfree(xfer);
            return -ENOMEM;
        }
        if (copy_from_user(page_address(page), ubuf, count)) {
            tfs_error("Failed to copy data from user\n");
            __free_page(page);
            kfree(xfer);
            atomic_inc(&tfs_ctx->write_errors);
            return -EFAULT;
        }
        ret = 1; // 模拟成功获取页面
        tfs_debug("Using copy transfer mode\n");
    }
    if (ret < 0) {
        tfs_error("Failed to pin user page (ret=%d)\n", ret);
        kfree(xfer);
        return ret;
    }
    if (ret != 1) {
        tfs_error("Failed to pin user page (ret=%d)\n", ret);
        kfree(xfer);
        return -EFAULT;
    }

    // 填充传输项
    xfer->page = page;
    xfer->size = count;
    xfer->offset = *ppos;
    xfer->pfn = page_to_pfn(page);
    INIT_LIST_HEAD(&xfer->list);

    tfs_debug("Created xfer: offset=%lld, size=%zu, pfn=%lu\n",
              (long long)xfer->offset, xfer->size, xfer->pfn);

    // 加入传输队列
    spin_lock(&tfs_ctx->lock);
    list_add_tail(&xfer->list, &tfs_ctx->xfer_list);
    spin_unlock(&tfs_ctx->lock);

    // 唤醒用户态守护进程
    wake_up_interruptible(&tfs_ctx->wq);
    
    *ppos += count;
    return count;
}

// 前向声明
static const struct inode_operations tfs_dir_inode_operations;
static const struct file_operations tfs_dir_operations;

// 文件读取函数
static ssize_t tfs_file_read(struct file *file, char __user *buf,
                           size_t count, loff_t *ppos)
{
    struct inode *inode = file_inode(file);
    struct tfs_xfer *xfer = NULL;
    ssize_t ret = 0;
    (void)inode;  // 显式标记未使用
    (void)ret;    // 显式标记未使用
    
    tfs_debug("tfs_file_read called: count=%zu, pos=%lld\n", count, *ppos);

    // 检查用户缓冲区
    if (!access_ok(buf, count))
        return -EFAULT;

    // 获取当前传输项
    spin_lock(&tfs_ctx->lock);
    if (!list_empty(&tfs_ctx->xfer_list)) {
        xfer = list_first_entry(&tfs_ctx->xfer_list, struct tfs_xfer, list);
        get_page(xfer->page); // 增加引用计数
    }
    spin_unlock(&tfs_ctx->lock);

    if (!xfer) {
        tfs_debug("No data available for reading\n");
        return 0; // 没有数据可读
    }

    // 计算实际可读字节数
    size_t avail = xfer->size - *ppos;
    if (avail <= 0)
        return 0;
    if (count > avail)
        count = avail;

    // 根据传输模式处理数据
    if (enable_zero_copy) {
        // 零拷贝模式
        void *kaddr = kmap(xfer->page);
        if (copy_to_user(buf, kaddr + *ppos, count)) {
            kunmap(xfer->page);
            put_page(xfer->page);
            atomic_inc(&tfs_ctx->read_errors);
            return -EFAULT;
        }
        kunmap(xfer->page);
        tfs_debug("Zero-copy read completed\n");
    } else {
        // 传统拷贝模式
        if (copy_to_user(buf, page_address(xfer->page) + *ppos, count)) {
            put_page(xfer->page);
            return -EFAULT;
        }
        tfs_debug("Copy mode read completed\n");
    }
    put_page(xfer->page);

    *ppos += count;
    return count;
}

static int tfs_file_release(struct inode *inode, struct file *file)
{
    struct tfs_inode_info *fsi = TFS_I(inode);
    (void)fsi;  // 显式标记未使用
    
    tfs_debug("tfs_file_release called for inode %lu\n", inode->i_ino);
    
    // 确保inode和文件指针有效
    if (!inode || !file) {
        tfs_error("NULL inode or file pointer in release\n");
        return -EINVAL;
    }
    
    // 清理任何与文件相关的资源
    if (tfs_ctx && tfs_ctx->current_xfer) {
        mutex_lock(&tfs_ctx->mmap_lock);
        if (tfs_ctx->current_xfer->page) {
            put_page(tfs_ctx->current_xfer->page);
        }
        tfs_ctx->current_xfer = NULL;
        mutex_unlock(&tfs_ctx->mmap_lock);
    }
    
    return 0;
}

static const struct file_operations tfs_file_ops = {
    .owner = THIS_MODULE,
    .read = tfs_file_read,
    .write = tfs_file_write,
    .llseek = generic_file_llseek,
    .open = generic_file_open,
    .release = tfs_file_release,
    .fsync = noop_fsync,
    .mmap = generic_file_mmap,
};

// 文件属性获取
static int tfs_getattr(struct mnt_idmap *idmap,
                      const struct path *path, struct kstat *stat,
                      u32 request_mask, unsigned int flags)
{
    struct inode *inode = d_inode(path->dentry);
    stat->blksize = PAGE_SIZE;
    stat->blocks = (inode->i_size + 511) >> 9;
    
    return 0;
}

// 文件属性设置
static int tfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
                      struct iattr *attr)
{
    struct inode *inode = d_inode(dentry);
    int error;
    
    tfs_debug("setattr called for inode %lu\n", inode->i_ino);

    error = setattr_prepare(idmap, dentry, attr);
    if (error)
        return error;

    if (attr->ia_valid & ATTR_SIZE) {
        error = inode_newsize_ok(inode, attr->ia_size);
        if (error)
            return error;
            
        truncate_setsize(inode, attr->ia_size);
        tfs_debug("File truncated to %lld bytes\n", attr->ia_size);
    }

    setattr_copy(idmap, inode, attr);
    return 0;
}

// 文件 inode 操作
static const struct inode_operations tfs_file_inode_operations = {
    .getattr = tfs_getattr,
    .setattr = tfs_setattr,
};

// 自定义文件创建函数，确保正确的权限设置
static int tfs_create(struct mnt_idmap *idmap, struct inode *dir, 
                     struct dentry *dentry, umode_t mode, bool excl)
{
    struct inode *inode;
    
    tfs_debug("tfs_create called for %s with mode %o\n", dentry->d_name.name, mode);
    tfs_info("Creating new file: %s\n", dentry->d_name.name);
    
    // 创建新的inode
    inode = new_inode(dir->i_sb);
    if (!inode) {
        tfs_error("Failed to allocate new inode for %s\n", dentry->d_name.name);
        return -ENOMEM;
    }
    
    // 设置为常规文件，确保所有用户都有读写权限
    inode->i_ino = get_next_ino();
    inode->i_mode = S_IFREG | 0666;  // 设置为666权限
    inode->i_uid = current_fsuid();
    inode->i_gid = current_fsgid();
    inode->i_blocks = 0;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
    inode->i_op = &tfs_file_inode_operations;
    inode->i_fop = &tfs_file_ops;
    
    // 设置文件大小为0
    i_size_write(inode, 0);
    
    // 添加到目录中
    d_instantiate(dentry, inode);
    dget(dentry);
    
    tfs_debug("File %s created successfully with inode %lu\n", 
              dentry->d_name.name, inode->i_ino);
    tfs_info("File %s created successfully with inode %lu, mode %o\n", 
             dentry->d_name.name, inode->i_ino, inode->i_mode);
    
    return 0;
}

// 自定义目录创建函数
static int tfs_mkdir(struct mnt_idmap *idmap, struct inode *dir, 
                    struct dentry *dentry, umode_t mode)
{
    struct inode *inode;
    
    tfs_debug("tfs_mkdir called for %s with mode %o\n", dentry->d_name.name, mode);
    
    // 创建新的inode
    inode = new_inode(dir->i_sb);
    if (!inode)
        return -ENOMEM;
    
    // 设置为目录，确保所有用户都有读写执行权限
    inode->i_ino = get_next_ino();
    inode->i_mode = S_IFDIR | 0777;  // 设置为777权限
    inode->i_uid = current_fsuid();
    inode->i_gid = current_fsgid();
    inode->i_blocks = 0;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
    inode->i_op = &tfs_dir_inode_operations;
    inode->i_fop = &tfs_dir_operations;
    
    // 设置目录项计数
    set_nlink(inode, 2);  // . 和 ..
    
    // 添加到父目录中
    d_instantiate(dentry, inode);
    dget(dentry);
    inc_nlink(dir);  // 使用inc_nlink增加父目录的链接计数
    
    tfs_debug("Directory %s created successfully with inode %lu\n", 
              dentry->d_name.name, inode->i_ino);
    
    return 0;
}

// 目录 inode 操作
static const struct inode_operations tfs_dir_inode_operations = {
    .lookup = simple_lookup,
    .create = tfs_create,  // 使用自定义的创建函数
    .link = simple_link,
    .unlink = simple_unlink,
    .mkdir = tfs_mkdir,    // 使用自定义的目录创建函数
    .rmdir = simple_rmdir,
    // 移除不支持的操作
    .rename = simple_rename,
};

// 自定义目录迭代函数
static int tfs_readdir(struct file *file, struct dir_context *ctx)
{
    struct inode *inode = file_inode(file);
    struct tfs_inode_info *fsi = TFS_I(inode);
    (void)fsi;  // 显式标记未使用
    int err = 0;
    
    tfs_debug("tfs_readdir called for inode %lu, pos %lld\n",
             inode->i_ino, ctx->pos);

    // 安全检查
    if (!inode) {
        tfs_error("NULL inode in readdir\n");
        return -EINVAL;
    }

    if (!S_ISDIR(inode->i_mode)) {
        tfs_error("readdir on non-directory inode %lu\n", inode->i_ino);
        return -ENOTDIR;
    }

    // 发出"."和".."条目
    if (!dir_emit_dots(file, ctx))
        return 0;

    // 简单的目录项实现 - 在实际应用中应该替换为真实的目录项遍历
    if (ctx->pos == 2) {
        if (!dir_emit(ctx, "testfile1", 9, inode->i_ino + 1, DT_REG))
            return 0;
        ctx->pos++;
    }
    if (ctx->pos == 3) {
        if (!dir_emit(ctx, "testdir1", 8, inode->i_ino + 2, DT_DIR))
            return 0;
        ctx->pos++;
    }

    return err;
}

// 文件系统统计信息
static int tfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    (void)dentry;  // 显式标记未使用
    
    tfs_debug("statfs called\n");

    buf->f_type = TFS_MAGIC;
    buf->f_bsize = PAGE_SIZE;
    buf->f_blocks = 0;  // 总块数 - 实际应用中应该计算
    buf->f_bfree = 0;   // 空闲块数
    buf->f_bavail = 0;  // 可用块数
    buf->f_files = 0;   // 总文件数
    buf->f_ffree = 0;   // 空闲文件数
    buf->f_namelen = NAME_MAX;
    
    return 0;
}

// 目录文件操作
static const struct file_operations tfs_dir_operations = {
    .read = generic_read_dir,
    .iterate_shared = tfs_readdir,  // 使用自定义实现
    .llseek = generic_file_llseek,
    .open = dcache_dir_open,
    .release = dcache_dir_close,
    .fsync = noop_fsync,
};

// 字符设备操作
static long tfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct tfs_xfer *xfer = NULL;
    int count = 0;
    
    tfs_debug("ioctl called: cmd=0x%x\n", cmd);
    
    if (!tfs_ctx) {
        tfs_error("tfs_ctx is NULL in ioctl\n");
        return -EINVAL;
    }
    
    switch (cmd) {
    case TFS_GET_XFER_COUNT:
        if (!arg) {
            return -EINVAL;
        }
        tfs_debug("ioctl in tfs_client process TFS_GET_XFER_COUNT: cmd=0x%x\n", cmd);
        spin_lock(&tfs_ctx->lock);
        list_for_each_entry(xfer, &tfs_ctx->xfer_list, list)
            count++;
        spin_unlock(&tfs_ctx->lock);
        
        if (copy_to_user((int __user *)arg, &count, sizeof(int))) {
            atomic_inc(&tfs_ctx->ioctl_errors);
            return -EFAULT;
        }
        return 0;
        
    case TFS_GET_XFER_INFO:
        if (!arg) {
            return -EINVAL;
        }
        
        tfs_debug("ioctl in tfs_client process TFS_GET_XFER_INFO: cmd=0x%x\n", cmd);
        spin_lock(&tfs_ctx->lock);
        if (!list_empty(&tfs_ctx->xfer_list)) {
            xfer = list_first_entry(&tfs_ctx->xfer_list,
                                   struct tfs_xfer, list);
            
            // 构造传输信息
            struct tfs_xfer_info info = {
                .offset = xfer->offset,
                .size = xfer->size,
                .pfn = xfer->pfn
            };
            
            spin_unlock(&tfs_ctx->lock);
            
            if (copy_to_user((struct tfs_xfer_info __user *)arg, &info, sizeof(info))) {
                atomic_inc(&tfs_ctx->ioctl_errors);
                return -EFAULT;
            }
            return 0;
        }
        spin_unlock(&tfs_ctx->lock);
        return -ENODATA;
        
     case TFS_RELEASE_XFER:
     tfs_debug("ioctl in tfs_client process TFS_RELEASE_XFER: cmd=0x%x\n", cmd);
        // 正确释放队列头部的 transfer
        spin_lock(&tfs_ctx->lock);
        if (!list_empty(&tfs_ctx->xfer_list)) {
            struct tfs_xfer *xfer = list_first_entry(&tfs_ctx->xfer_list, struct tfs_xfer, list);
            list_del(&xfer->list);
            if (xfer->page)
                put_page(xfer->page);
            kfree(xfer);
        }
        spin_unlock(&tfs_ctx->lock);
        return 0;
    default:
        return -ENOTTY;
    }
    return 0;
}

// 实现mmap操作
static int tfs_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct tfs_xfer *xfer = NULL;
    unsigned long vsize = vma->vm_end - vma->vm_start;
    int ret;
    
    tfs_debug("mmap called: start=%lx, end=%lx, size=%lu\n",
              vma->vm_start, vma->vm_end, vsize);

    if (!tfs_ctx) {
        tfs_error("tfs_ctx is NULL in mmap\n");
        return -EINVAL;
    }

    // 检查请求的映射大小
    if (vsize == 0 || vsize > PAGE_SIZE) {
        tfs_error("Invalid mmap size: %lu\n", vsize);
        return -EINVAL;
    }

    // 保护对当前传输项的访问
    mutex_lock(&tfs_ctx->mmap_lock);
    spin_lock(&tfs_ctx->lock);
    
    if (list_empty(&tfs_ctx->xfer_list)) {
        spin_unlock(&tfs_ctx->lock);
        mutex_unlock(&tfs_ctx->mmap_lock);
        tfs_error("No xfer available for mmap\n");
        return -EINVAL;
    }
    
    xfer = list_first_entry(&tfs_ctx->xfer_list, struct tfs_xfer, list);
    if (!xfer) {
        spin_unlock(&tfs_ctx->lock);
        mutex_unlock(&tfs_ctx->mmap_lock);
        tfs_error("Invalid xfer in xfer_list\n");
        return -EINVAL;
    }
    
    // 检查是否是空文件的特殊传输项
    if (!xfer->page) {
        tfs_debug("Empty file transfer detected in mmap, size=%zu\n", xfer->size);
        spin_unlock(&tfs_ctx->lock);
        mutex_unlock(&tfs_ctx->mmap_lock);
        // 对于空文件，我们不需要映射，但也不应该报错
        // 返回成功，但不执行实际映射
        return 0;
    }
    
    get_page(xfer->page); // 增加页面引用计数
    spin_unlock(&tfs_ctx->lock);

    // 设置VMA标志
    vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
    vma->vm_private_data = xfer;

    // 直接映射物理页面到用户空间
    ret = remap_pfn_range(vma, vma->vm_start,
                         page_to_pfn(xfer->page),
                         vsize, vma->vm_page_prot);
    
    if (ret) {
        tfs_error("remap_pfn_range failed: %d\n", ret);
        atomic_inc(&tfs_ctx->mmap_errors);
        put_page(xfer->page);
    } else {
        tfs_debug("mmap succeeded for pfn=%lu\n", page_to_pfn(xfer->page));
        tfs_ctx->current_xfer = xfer;
    }
    
    mutex_unlock(&tfs_ctx->mmap_lock);
    return ret;
}

static int tfs_release(struct inode *inode, struct file *file)
{
    tfs_debug("tfs_release called\n");
    return 0;
}

static unsigned int tfs_poll(struct file *file, poll_table *wait)
{
    unsigned int mask = 0;
    
    poll_wait(file, &tfs_ctx->wq, wait);
    spin_lock(&tfs_ctx->lock);
    if (!list_empty(&tfs_ctx->xfer_list))
        mask |= POLLIN | POLLRDNORM;
    spin_unlock(&tfs_ctx->lock);
    
    tfs_debug("poll called, mask=%u\n", mask);
    return mask;
}
static const struct file_operations tfs_chardev_ops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = tfs_ioctl,
    .mmap = tfs_mmap,
    .release = tfs_release,
    .poll = tfs_poll,
};

//================ 文件系统操作 ========================

static void tfs_put_super(struct super_block *sb)
{
    struct tfs_fs_info *fsi;
    
    tfs_debug("put_super called\n");
    
    if (!sb) {
        tfs_error("NULL superblock in put_super\n");
        return;
    }
    
    fsi = sb->s_fs_info;
    if (fsi) {
        // 清理文件系统特定的资源
        sb->s_fs_info = NULL;
        kfree(fsi);
    }
    
    // 确保所有挂起的传输都被清理
    if (tfs_ctx) {
        struct tfs_xfer *xfer, *tmp;
        
        spin_lock(&tfs_ctx->lock);
        list_for_each_entry_safe(xfer, tmp, &tfs_ctx->xfer_list, list) {
            list_del(&xfer->list);
            if (xfer->page) {
                if (enable_zero_copy && page_mapped(xfer->page)) {
                    kunmap(xfer->page);
                    tfs_debug("Unmapped zero-copy page\n");
                }
                put_page(xfer->page);
                if (!enable_zero_copy) {
                    tfs_debug("Freed copy mode page\n");
                }
            }
            kfree(xfer);
        }
        int transfer_count = 0;
        list_for_each_entry_safe(xfer, tmp, &tfs_ctx->xfer_list, list) {
            transfer_count++;
        }
        spin_unlock(&tfs_ctx->lock);
        tfs_info("Cleaned up %d pending transfers\n", transfer_count);
    }
    
    tfs_debug("Superblock cleanup completed\n");
}

static struct super_operations tfs_super_ops = {
    .alloc_inode = tfs_alloc_inode,
    .free_inode = tfs_free_inode,
    .put_super = tfs_put_super,
    .statfs = tfs_statfs,
    .drop_inode = generic_delete_inode,
};

static int tfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
    struct tfs_fs_info *fsi;
    struct inode *inode;
    
    tfs_debug("fill_super called\n");
    
    // 分配文件系统信息结构
    fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
    if (!fsi) {
        tfs_error("Failed to allocate fs_info\n");
        return -ENOMEM;
    }
    
    // 设置超级块参数
    sb->s_blocksize = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT_4K;
    sb->s_magic = TFS_MAGIC;
    sb->s_op = &tfs_super_ops;
    sb->s_time_gran = 1;
    sb->s_fs_info = fsi;
    
    // 创建根inode
    inode = new_inode(sb);
    if (!inode) {
        tfs_error("Failed to allocate root inode\n");
        kfree(fsi);
        return -ENOMEM;
    }
    
    // 设置inode属性，确保所有用户都有读写权限
    inode->i_ino = 1;
    inode->i_mode = S_IFDIR | 0777;  // 修改为777权限
    inode->i_uid = current_fsuid();   // 使用当前用户的UID
    inode->i_gid = current_fsgid();   // 使用当前用户的GID
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
    inode->i_op = &tfs_dir_inode_operations;
    inode->i_fop = &tfs_dir_operations;
    
    // 设置目录项计数
    set_nlink(inode, 2);  // . 和 ..
    
    // 创建根目录项
    sb->s_root = d_make_root(inode);
    if (!sb->s_root) {
        tfs_error("Failed to create root dentry\n");
        iput(inode);
        kfree(fsi);
        return -ENOMEM;
    }
    
    tfs_debug("Superblock filled successfully\n");
    return 0;
}

// 文件系统上下文操作
static int tfs_get_tree(struct fs_context *fc)
{
    tfs_debug("get_tree called\n");
    // 使用安全的方式调用 get_tree_nodev
    return get_tree_nodev(fc, tfs_fill_super);
}

static void tfs_free_fc(struct fs_context *fc)
{
    tfs_debug("free_fc called\n");
    kfree(fc->s_fs_info);
}

static const struct fs_context_operations tfs_context_ops = {
    .free    = tfs_free_fc,
    .get_tree = tfs_get_tree,
};

static int tfs_init_fs_context(struct fs_context *fc)
{
    tfs_debug("init_fs_context called\n");
    fc->ops = &tfs_context_ops;
    return 0;
}

// 文件系统类型定义
static struct file_system_type tfs_fs_type = {
    .owner = THIS_MODULE,
    .name = "tfs",
    .init_fs_context = tfs_init_fs_context,
    .kill_sb = kill_anon_super,
};


// 模块参数定义
static unsigned int max_files = 1000;
module_param(max_files, uint, 0644);
MODULE_PARM_DESC(max_files, "Maximum number of files supported");

static unsigned int debug_level = 1;
module_param(debug_level, uint, 0644);
MODULE_PARM_DESC(debug_level, "Debug level (0-3)");

module_param(enable_zero_copy, bool, 0644);
MODULE_PARM_DESC(enable_zero_copy, "Enable zero-copy transfers");

//================ 模块初始化 ========================

// 错误统计显示函数
static void tfs_print_error_stats(void)
{
    if (!tfs_ctx) return;
    
    tfs_info("TFS Error Statistics:\n");
    tfs_info("- Read errors: %d\n", atomic_read(&tfs_ctx->read_errors));
    tfs_info("- Write errors: %d\n", atomic_read(&tfs_ctx->write_errors));
    tfs_info("- IOCTL errors: %d\n", atomic_read(&tfs_ctx->ioctl_errors));
    tfs_info("- MMAP errors: %d\n", atomic_read(&tfs_ctx->mmap_errors));
}

static int __init tfs_init(void)
{
    int ret;
    
    tfs_info("Initializing TFS module\n");

    // 创建inode缓存
    tfs_inode_cachep = kmem_cache_create("tfs_inode_cache",
                                        sizeof(struct tfs_inode_info),
                                        0,
                                        SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|SLAB_ACCOUNT,
                                        NULL);
    if (!tfs_inode_cachep) {
        tfs_error("Failed to create inode cache\n");
        return -ENOMEM;
    }

    // 分配全局上下文
    tfs_ctx = kzalloc(sizeof(*tfs_ctx), GFP_KERNEL);
    if (!tfs_ctx) {
        tfs_error("Failed to allocate context\n");
        kmem_cache_destroy(tfs_inode_cachep);
        return -ENOMEM;
    }

    // 初始化队列和锁
    INIT_LIST_HEAD(&tfs_ctx->xfer_list);
    spin_lock_init(&tfs_ctx->lock);
    init_waitqueue_head(&tfs_ctx->wq);
    mutex_init(&tfs_ctx->mmap_lock);
    tfs_ctx->current_xfer = NULL;

    // 创建设备节点
    tfs_ctx->mdev.minor = MISC_DYNAMIC_MINOR;
    tfs_ctx->mdev.name = "tfs_ctl";
    tfs_ctx->mdev.fops = &tfs_chardev_ops;
    tfs_ctx->mdev.mode = 0666;
    
    ret = misc_register(&tfs_ctx->mdev);
    if (ret) {
        tfs_error("misc_register failed: %d\n", ret);
        kfree(tfs_ctx);
        kmem_cache_destroy(tfs_inode_cachep);
        return ret;
    }

    // 注册文件系统
    ret = register_filesystem(&tfs_fs_type);
    if (ret) {
        tfs_error("register_filesystem failed: %d\n", ret);
        misc_deregister(&tfs_ctx->mdev);
        kfree(tfs_ctx);
        kmem_cache_destroy(tfs_inode_cachep);
        return ret;
    }

    tfs_info("TFS module loaded successfully with parameters:\n");
    tfs_info("- max_files: %u\n", max_files);
    tfs_info("- debug_level: %u\n", debug_level);
    tfs_info("- enable_zero_copy: %d\n", enable_zero_copy);
    
    // 根据debug_level设置调试输出
    if (debug_level == 0) {
        #undef TFS_DEBUG
        #define TFS_DEBUG 0
    } else if (debug_level > 1) {
        #undef tfs_debug
        #define tfs_debug(fmt, ...) printk(KERN_DEBUG "TFS DEBUG[%s:%d]: " fmt, __func__, __LINE__, ##__VA_ARGS__)
    }
    
    return 0;
}

// 清理函数
static void __exit tfs_exit(void)
{
    struct tfs_xfer *xfer, *tmp;
    
    tfs_info("Unloading TFS module\n");
    
    // 取消文件系统注册
    unregister_filesystem(&tfs_fs_type);
    
    // 检查全局上下文是否存在
    if (tfs_ctx) {
        // 取消杂项设备注册
        misc_deregister(&tfs_ctx->mdev);
        
        // 清理所有待处理传输
        spin_lock(&tfs_ctx->lock);
        list_for_each_entry_safe(xfer, tmp, &tfs_ctx->xfer_list, list) {
            list_del(&xfer->list);
            if (xfer->page) {
                if (page_mapped(xfer->page))
                    kunmap(xfer->page);
                put_page(xfer->page);
            }
            kfree(xfer);
        }
        spin_unlock(&tfs_ctx->lock);
        
        // 释放上下文
        kfree(tfs_ctx);
        tfs_ctx = NULL;
    }
    
    // 销毁inode缓存
    if (tfs_inode_cachep) {
        kmem_cache_destroy(tfs_inode_cachep);
        tfs_inode_cachep = NULL;
    }
    
    // 显示错误统计
    tfs_print_error_stats();
    tfs_info("TFS module unloaded\n");
}

module_init(tfs_init);
module_exit(tfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TFS Development Team");
MODULE_DESCRIPTION("TFS Distributed Filesystem Client - Robust Implementation");
MODULE_VERSION("1.0");