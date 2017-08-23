/*
 * RedirFS: Redirecting File System
 * Written by Frantisek Hrbata <frantisek.hrbata@redirfs.org>
 *
 * History:
 * 2017 - changing for the latest kernels by Slava Imameev
 *
 * Copyright 2008 - 2010 Frantisek Hrbata
 * All rights reserved.
 *
 * This file is part of RedirFS.
 *
 * RedirFS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RedirFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RedirFS. If not, see <http://www.gnu.org/licenses/>.
 */

#include "rfs.h"

#ifdef DBG
    #pragma GCC push_options
    #pragma GCC optimize ("O0")
#endif // DBG

static rfs_kmem_cache_t *rfs_file_cache = NULL;

struct file_operations rfs_file_ops = {
	.open = rfs_open
};

static struct rfs_file *rfs_file_alloc(struct file *file)
{
	struct rfs_file *rfile;

	rfile = kmem_cache_zalloc(rfs_file_cache, GFP_KERNEL);
	if (!rfile)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&rfile->rdentry_list);
	INIT_LIST_HEAD(&rfile->data);
	rfile->file = file;
	spin_lock_init(&rfile->lock);
	atomic_set(&rfile->count, 1);
	rfile->op_old = fops_get(file->f_op);

	if (rfile->op_old)
		memcpy(&rfile->op_new, rfile->op_old,
				sizeof(struct file_operations));

    //
    // unconditionally register open operation to be notified
    // of open requests, some devices do not register open
    // operation, e.g. null_fops, but RedirFS requires
    // open operation to be called through file_operations
    //
    rfile->op_new.open = rfs_open;

	return rfile;
}

struct rfs_file *rfs_file_get(struct rfs_file *rfile)
{
	if (!rfile || IS_ERR(rfile))
		return NULL;

	BUG_ON(!atomic_read(&rfile->count));
	atomic_inc(&rfile->count);

	return rfile;
}

void rfs_file_put(struct rfs_file *rfile)
{
	if (!rfile || IS_ERR(rfile))
		return;

	BUG_ON(!atomic_read(&rfile->count));
	if (!atomic_dec_and_test(&rfile->count))
		return;

	rfs_dentry_put(rfile->rdentry);
	fops_put(rfile->op_old);

	rfs_data_remove(&rfile->data);
	kmem_cache_free(rfs_file_cache, rfile);
}

static struct rfs_file *rfs_file_add(struct file *file)
{
	struct rfs_file *rfile;

	rfile = rfs_file_alloc(file);
	if (IS_ERR(rfile))
		return rfile;

	rfile->rdentry = rfs_dentry_find(file->f_dentry);
	rfs_dentry_add_rfile(rfile->rdentry, rfile);
	fops_put(file->f_op);
	file->f_op = &rfile->op_new;
	rfs_file_get(rfile);
	spin_lock(&rfile->rdentry->lock);
	rfs_file_set_ops(rfile);
	spin_unlock(&rfile->rdentry->lock);

	return rfile;
}

static void rfs_file_del(struct rfs_file *rfile)
{
	rfs_dentry_rem_rfile(rfile);
	rfile->file->f_op = fops_get(rfile->op_old);
	rfs_file_put(rfile);
}

int rfs_file_cache_create(void)
{
	rfs_file_cache = rfs_kmem_cache_create("rfs_file_cache",
			sizeof(struct rfs_file));

	if (!rfs_file_cache)
		return -ENOMEM;

	return 0;
}

void rfs_file_cache_destory(void)
{
	kmem_cache_destroy(rfs_file_cache);
}

int rfs_open(struct inode *inode, struct file *file)
{
	struct rfs_file *rfile;
	struct rfs_dentry *rdentry;
	struct rfs_inode *rinode;
	struct rfs_info *rinfo;
	struct rfs_context rcont;
	struct redirfs_args rargs;

	rinode = rfs_inode_find(inode);
	fops_put(file->f_op);
	file->f_op = fops_get(rinode->fop_old);

	rdentry = rfs_dentry_find(file->f_dentry);
	if (!rdentry) {
		rfs_inode_put(rinode);
		if (file->f_op && file->f_op->open)
			return file->f_op->open(inode, file);

		return 0;
	}

	rinfo = rfs_dentry_get_rinfo(rdentry);
	rfs_dentry_put(rdentry);
	rfs_context_init(&rcont, 0);

	if (S_ISREG(inode->i_mode))
		rargs.type.id = REDIRFS_REG_FOP_OPEN;
	else if (S_ISDIR(inode->i_mode))
		rargs.type.id = REDIRFS_DIR_FOP_OPEN;
	else if (S_ISLNK(inode->i_mode))
		rargs.type.id = REDIRFS_LNK_FOP_OPEN;
	else if (S_ISCHR(inode->i_mode))
		rargs.type.id = REDIRFS_CHR_FOP_OPEN;
	else if (S_ISBLK(inode->i_mode))
		rargs.type.id = REDIRFS_BLK_FOP_OPEN;
	else if (S_ISFIFO(inode->i_mode))
		rargs.type.id = REDIRFS_FIFO_FOP_OPEN;
    else
        BUG();

	rargs.args.f_open.inode = inode;
	rargs.args.f_open.file = file;

	if (!rfs_precall_flts(rinfo->rchain, &rcont, &rargs)) {
		if (rinode->fop_old && rinode->fop_old->open)
			rargs.rv.rv_int = rinode->fop_old->open(
					rargs.args.f_open.inode,
					rargs.args.f_open.file);
		else
			rargs.rv.rv_int = 0;
	}

	if (!rargs.rv.rv_int) {
		rfile = rfs_file_add(file);
		if (IS_ERR(rfile))
			BUG();
		rfs_file_put(rfile);
	}

	rfs_postcall_flts(rinfo->rchain, &rcont, &rargs);
	rfs_context_deinit(&rcont);

	rfs_inode_put(rinode);
	rfs_info_put(rinfo);
	return rargs.rv.rv_int;
}

static int rfs_release(struct inode *inode, struct file *file)
{
	struct rfs_file *rfile;
	struct rfs_info *rinfo;
	struct rfs_context rcont;
	struct redirfs_args rargs;

	rfile = rfs_file_find(file);
	rinfo = rfs_dentry_get_rinfo(rfile->rdentry);
	rfs_context_init(&rcont, 0);

	if (S_ISREG(inode->i_mode))
		rargs.type.id = REDIRFS_REG_FOP_RELEASE;
	else if (S_ISDIR(inode->i_mode))
		rargs.type.id = REDIRFS_DIR_FOP_RELEASE;
	else if (S_ISLNK(inode->i_mode))
		rargs.type.id = REDIRFS_LNK_FOP_RELEASE;
	else if (S_ISCHR(inode->i_mode))
		rargs.type.id = REDIRFS_CHR_FOP_RELEASE;
	else if (S_ISBLK(inode->i_mode))
		rargs.type.id = REDIRFS_BLK_FOP_RELEASE;
	else if (S_ISFIFO(inode->i_mode))
		rargs.type.id = REDIRFS_FIFO_FOP_RELEASE;

	rargs.args.f_release.inode = inode;
	rargs.args.f_release.file = file;

	if (!rfs_precall_flts(rinfo->rchain, &rcont, &rargs)) {
		if (rfile->op_old && rfile->op_old->release)
			rargs.rv.rv_int = rfile->op_old->release(
					rargs.args.f_release.inode,
					rargs.args.f_release.file);
		else
			rargs.rv.rv_int = 0;
	}

	rfs_postcall_flts(rinfo->rchain, &rcont, &rargs);
	rfs_context_deinit(&rcont);

	rfs_file_del(rfile);
	rfs_file_put(rfile);
	rfs_info_put(rinfo);
	return rargs.rv.rv_int;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0))
static int rfs_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	LIST_HEAD(sibs);
	struct rfs_dcache_entry *sib;
	struct rfs_file *rfile;
	struct rfs_info *rinfo;
	struct rfs_context rcont;
	struct rfs_dentry *rdentry;
	struct redirfs_args rargs;

	rfile = rfs_file_find(file);
	rinfo = rfs_dentry_get_rinfo(rfile->rdentry);
	rfs_context_init(&rcont, 0);

	if (S_ISDIR(file->f_dentry->d_inode->i_mode)) {
		rargs.type.id = REDIRFS_DIR_FOP_READDIR;

	    rargs.args.f_readdir.file = file;
	    rargs.args.f_readdir.dirent = dirent;
	    rargs.args.f_readdir.filldir = filldir;

	    if (!rfs_precall_flts(rinfo->rchain, &rcont, &rargs)) {
		    if (rfile->op_old && rfile->op_old->readdir) 
			    rargs.rv.rv_int = rfile->op_old->readdir(
					    rargs.args.f_readdir.file,
					    rargs.args.f_readdir.dirent,
					    rargs.args.f_readdir.filldir);
		    else
			    rargs.rv.rv_int = -ENOTDIR;
	    }

	    rfs_postcall_flts(rinfo->rchain, &rcont, &rargs);
	    rfs_context_deinit(&rcont);

    } else {
        rargs.rv.rv_int = -ENOTDIR;
    }

	if (rargs.rv.rv_int)
		goto exit;

	if (rfs_dcache_get_subs(file->f_dentry, &sibs)) {
		BUG();
		goto exit;
	}

	list_for_each_entry(sib, &sibs, list) {
		rdentry = rfs_dentry_find(sib->dentry);
		if (rdentry) {
			rfs_dentry_put(rdentry);
			continue;
		}

		if (!rinfo->rops) {
			if (!sib->dentry->d_inode)
				continue;

			if (!S_ISDIR(sib->dentry->d_inode->i_mode))
				continue;
		}

		if (rfs_dcache_rdentry_add(sib->dentry, rinfo)) {
			BUG();
			goto exit;
		}
	}

exit:
	rfs_dcache_entry_free_list(&sibs);
	rfs_file_put(rfile);
	rfs_info_put(rinfo);
	return rargs.rv.rv_int;
}
#endif

extern loff_t rfs_llseek(struct file *file, loff_t offset, int origin);
extern ssize_t rfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos);
extern ssize_t rfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,0))
extern ssize_t rfs_read_iter(struct kiocb *kiocb, struct iov_iter *iov_iter);
extern ssize_t rfs_write_iter(struct kiocb *kiocb, struct iov_iter *iov_iter);
#endif
extern int rfs_iterate(struct file *file, struct dir_context *dir_context);
extern int rfs_iterate_shared(struct file *file, struct dir_context *dir_context);
extern unsigned int rfs_poll(struct file *file, struct poll_table_struct *poll_table_struct);
extern long rfs_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
extern long rfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
extern int rfs_mmap(struct file *file, struct vm_area_struct *vma);
extern int rfs_flush(struct file *, fl_owner_t owner);
extern int rfs_fsync(struct file *file, loff_t start, loff_t end, int datasync);
extern int rfs_fasync(int fd, struct file *file, int on);
extern int rfs_lock(struct file *file, int cmd, struct file_lock *flock);
extern ssize_t rfs_sendpage(struct file *file, struct page *page, int offset,
                     size_t len, loff_t *pos, int more);
extern unsigned long rfs_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags);
extern int rfs_flock(struct file *file, int cmd, struct file_lock *flock);
extern ssize_t rfs_splice_write(struct pipe_inode_info *pipe, struct file *out,
			  loff_t *ppos, size_t len, unsigned int flags);
extern ssize_t rfs_splice_read(struct file *in, loff_t *ppos,
				 struct pipe_inode_info *pipe, size_t len,
				 unsigned int flags);
extern int rfs_setlease(struct file *file, long arg, struct file_lock **flock,
		  void **priv);
extern long rfs_fallocate(struct file *file, int mode,
			  loff_t offset, loff_t len);
extern void rfs_show_fdinfo(struct seq_file *seq_file, struct file *file);
extern ssize_t rfs_copy_file_range(struct file *file_in, loff_t pos_in,
				    struct file *file_out, loff_t pos_out,
				    size_t count, unsigned int flags);
extern int rfs_clone_file_range(struct file *src_file, loff_t src_off,
		struct file *dst_file, loff_t dst_off, u64 count);
extern ssize_t rfs_dedupe_file_range(struct file *src_file, u64 loff,
                    u64 len, struct file *dst_file, u64 dst_loff);

static void rfs_file_set_ops_reg(struct rfs_file *rfile)
{
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_LLSEEK, llseek, rfs_llseek);
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_READ, read, rfs_read);
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_WRITE, write, rfs_write);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,0))
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_READ_ITER, read_iter, rfs_read_iter);
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_WRITE_ITER, write_iter, rfs_write_iter);
#endif
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_POLL, poll, rfs_poll);
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_UNLOCKED_IOCTL, unlocked_ioctl, rfs_unlocked_ioctl);
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_COMPAT_IOCTL, compat_ioctl, rfs_compat_ioctl);
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_MMAP, mmap, rfs_mmap);
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_OPEN, open, rfs_open); // should not be called as called through rfile->op_new.open registered on inode lookup
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_FLUSH, flush, rfs_flush);
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_FSYNC, fsync, rfs_fsync);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_fasync), fasync, rfs_fasync);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_lock), lock, rfs_lock);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_sendpage), sendpage, rfs_sendpage);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_get_unmapped_area), get_unmapped_area, rfs_get_unmapped_area);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_flock), flock, rfs_flock);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_splice_write), splice_write, rfs_splice_write);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_splice_read), splice_read, rfs_splice_read);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_setlease), setlease, rfs_setlease);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_fallocate), fallocate, rfs_fallocate);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_show_fdinfo), show_fdinfo, rfs_show_fdinfo);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_copy_file_range), copy_file_range, rfs_copy_file_range);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_clone_file_range), clone_file_range, rfs_clone_file_range);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_REG, RFS_OP_f_dedupe_file_range), dedupe_file_range, rfs_dedupe_file_range);
}

static void rfs_file_set_ops_dir(struct rfs_file *rfile)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0))
	rfile->op_new.readdir = rfs_readdir;
#else
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_DIR_ITERATE, iterate, rfs_iterate);
    RFS_SET_FOP(rfile, REDIRFS_REG_FOP_DIR_ITERATE_SHARED, iterate_shared, rfs_iterate_shared);
#endif
}

static void rfs_file_set_ops_lnk(struct rfs_file *rfile)
{
}

static void rfs_file_set_ops_chr(struct rfs_file *rfile)
{
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_llseek), llseek, rfs_llseek);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_read), read, rfs_read);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_write), write, rfs_write);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,0))
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_read_iter), read_iter, rfs_read_iter);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_write_iter), write_iter, rfs_write_iter);
#endif
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_poll), poll, rfs_poll);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_unlocked_ioctl), unlocked_ioctl, rfs_unlocked_ioctl);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_compat_ioctl), compat_ioctl, rfs_compat_ioctl);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_mmap), mmap, rfs_mmap);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_open), open, rfs_open); // should not be called as called through rfile->op_new.open registered on inode lookup
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_flush), flush, rfs_flush);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_fsync), fsync, rfs_fsync);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_fasync), fasync, rfs_fasync);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_lock), lock, rfs_lock);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_sendpage), sendpage, rfs_sendpage);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_get_unmapped_area), get_unmapped_area, rfs_get_unmapped_area);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_flock), flock, rfs_flock);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_splice_write), splice_write, rfs_splice_write);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_splice_read), splice_read, rfs_splice_read);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_setlease), setlease, rfs_setlease);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_fallocate), fallocate, rfs_fallocate);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_show_fdinfo), show_fdinfo, rfs_show_fdinfo);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_copy_file_range), copy_file_range, rfs_copy_file_range);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_clone_file_range), clone_file_range, rfs_clone_file_range);
    RFS_SET_FOP(rfile, RFS_OP_IDC(RFS_INODE_CHAR, RFS_OP_f_dedupe_file_range), dedupe_file_range, rfs_dedupe_file_range);
}

static void rfs_file_set_ops_blk(struct rfs_file *rfile)
{
}

static void rfs_file_set_ops_fifo(struct rfs_file *rfile)
{
}

void rfs_file_set_ops(struct rfs_file *rfile)
{
	umode_t mode;

	if (!rfile->rdentry->rinode)
		return;

	mode = rfile->rdentry->rinode->inode->i_mode;

	if (S_ISREG(mode))
		rfs_file_set_ops_reg(rfile);

	else if (S_ISDIR(mode))
		rfs_file_set_ops_dir(rfile);

	else if (S_ISLNK(mode))
		rfs_file_set_ops_lnk(rfile);

	else if (S_ISCHR(mode))
		rfs_file_set_ops_chr(rfile);

	else if (S_ISBLK(mode))
		rfs_file_set_ops_blk(rfile);

	else if (S_ISFIFO(mode))
		rfs_file_set_ops_fifo(rfile);

    //
    // unconditionally set release hook to match open hooks
    //
    rfile->op_new.release = rfs_release;
}

#ifdef DBG
    #pragma GCC pop_options
#endif // DBG
