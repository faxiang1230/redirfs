/*
 * DummyFlt: Dummy Filter
 * Written by Frantisek Hrbata <frantisek.hrbata@redirfs.org>
 *
 * History:
 *   2008 - 2010 Frantisek Hrbata
 *   2017 - modified for new kernels by Slava Imameev
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

#include <redirfs.h>
#include <rfs.h>
#include <linux/slab.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
    #include <linux/mount.h>
#endif
#include <linux/delay.h>

#ifndef f_dentry
    #define f_dentry    f_path.dentry
#endif

#ifndef f_vfsmnt
    #define f_vfsmnt    f_path.mnt
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0))
    #define f_inode f_path.dentry->d_inode
#endif

#define DUMMYFLT_VERSION "0.5"

static redirfs_filter dummyflt;
struct redirfs_path_info dummyflt_path_info;

static struct redirfs_filter_info dummyflt_info = {
    .owner = THIS_MODULE,
    .name = "dummyflt",
    .priority = 500000000,
    .active = 1
};

const char* imode_to_str(umode_t i_mode)
{
    if (S_ISREG(i_mode))
        return "REG";
    else if (S_ISDIR(i_mode))
        return "DIR";
    else if (S_ISLNK(i_mode))
        return "LINK";
    else if (S_ISCHR(i_mode))
        return "CHAR";
    else if (S_ISBLK(i_mode))
        return "BLK";
    else if (S_ISFIFO(i_mode))
        return "FIFO";
    else if (S_ISSOCK(i_mode))
        return "SOCK";
    else
        return "UNKNOWN";
}

static void *dummyflt_alloc(size_t size)
{
    void *p;
    
    p = kmalloc(size, GFP_KERNEL);
    if (!p)
        return NULL;

    memset(p, 0, size);

    return p;
}

enum redirfs_rv dummyflt_open(redirfs_context context,
        struct redirfs_args *args)
{
    char *path;
    char *call;
    int   rv;
    const char* imode = imode_to_str(args->args.f_open.file->f_inode->i_mode);

    path = dummyflt_alloc(sizeof(char) * PAGE_SIZE);
    if (!path)
        return REDIRFS_CONTINUE;

    rv = redirfs_get_filename(args->args.f_open.file->f_vfsmnt,
            args->args.f_open.file->f_dentry, path, PAGE_SIZE);

    if (rv) {
        printk(KERN_ERR "dummyflt: rfs_get_filename failed(%d)\n", rv);
        goto exit;
    }

    call = args->type.call == REDIRFS_PRECALL ? "precall" : "postcall";

    printk(KERN_ALERT "dummyflt: open: %s [%s][%lx], call: %s\n", 
           path, imode, (unsigned long)args->args.f_open.file,call);

exit:
    kfree(path);
    return REDIRFS_CONTINUE;
}

enum redirfs_rv dummyflt_release(redirfs_context context,
        struct redirfs_args *args)
{
    char *path;
    char *call;
    int rv;

    path = dummyflt_alloc(sizeof(char) * PAGE_SIZE);
    if (!path)
        return REDIRFS_CONTINUE;

    rv = redirfs_get_filename(args->args.f_release.file->f_vfsmnt,
            args->args.f_release.file->f_dentry, path, PAGE_SIZE);

    if (rv) {
        printk(KERN_ERR "dummyflt: rfs_get_filename failed(%d)\n", rv);
        goto exit;
    }

    call = args->type.call == REDIRFS_PRECALL ? "precall" : "postcall";

    printk(KERN_ALERT "dummyflt: release: %s [%lx], call: %s\n", 
           path, (unsigned long)args->args.f_release.file, call);

exit:
    kfree(path);
    return REDIRFS_CONTINUE;
}

enum redirfs_rv dummyflt_read(redirfs_context context,
        struct redirfs_args *args)
{
    char *path;
    char *call;
    int rv;

    path = dummyflt_alloc(sizeof(char) * PAGE_SIZE);
    if (!path)
        return REDIRFS_CONTINUE;

    rv = redirfs_get_filename(args->args.f_read.file->f_vfsmnt,
            args->args.f_read.file->f_dentry, path, PAGE_SIZE);

    if (rv) {
        printk(KERN_ERR "dummyflt: rfs_get_filename failed(%d)\n", rv);
        goto exit;
    }

    call = args->type.call == REDIRFS_PRECALL ? "precall" : "postcall";

    printk(KERN_ALERT "dummyflt: read: %s, call: %s\n", path, call);

exit:
    kfree(path);
    return REDIRFS_CONTINUE;
}

enum redirfs_rv dummyflt_readpage(redirfs_context context,
        struct redirfs_args *args)
{
    char *path;
    char *call;
    int rv;

    path = dummyflt_alloc(sizeof(char) * PAGE_SIZE);
    if (!path)
        return REDIRFS_CONTINUE;

    rv = redirfs_get_filename(args->args.a_readpage.file->f_vfsmnt,
            args->args.a_readpage.file->f_dentry, path, PAGE_SIZE);

    if (rv) {
        printk(KERN_ERR "dummyflt: rfs_get_filename failed(%d)\n", rv);
        goto exit;
    }

    call = args->type.call == REDIRFS_PRECALL ? "precall" : "postcall";

    printk(KERN_ALERT "dummyflt: readpage: %s, call: %s\n", path, call);

exit:
    kfree(path);
    return REDIRFS_CONTINUE;
}

enum redirfs_rv dummyflt_readpages(redirfs_context context,
        struct redirfs_args *args)
{
    char *path;
    char *call;
    int rv;

    path = dummyflt_alloc(sizeof(char) * PAGE_SIZE);
    if (!path)
        return REDIRFS_CONTINUE;

    rv = redirfs_get_filename(args->args.a_readpages.file->f_vfsmnt,
            args->args.a_readpages.file->f_dentry, path, PAGE_SIZE);

    if (rv) {
        printk(KERN_ERR "dummyflt: rfs_get_filename failed(%d)\n", rv);
        goto exit;
    }

    call = args->type.call == REDIRFS_PRECALL ? "precall" : "postcall";

    printk(KERN_ALERT "dummyflt: readpage: %s, call: %s\n", path, call);

exit:
    kfree(path);
    return REDIRFS_CONTINUE;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,0))
enum redirfs_rv dummyflt_read_iter(redirfs_context context,
        struct redirfs_args *args)
{
    char *path;
    char *call;
    int rv;

    path = dummyflt_alloc(sizeof(char) * PAGE_SIZE);
    if (!path)
        return REDIRFS_CONTINUE;

    rv = redirfs_get_filename(args->args.f_read_iter.kiocb->ki_filp->f_vfsmnt,
            args->args.f_read_iter.kiocb->ki_filp->f_dentry, path, PAGE_SIZE);

    if (rv) {
        printk(KERN_ERR "dummyflt: rfs_get_filename failed(%d)\n", rv);
        goto exit;
    }

    call = args->type.call == REDIRFS_PRECALL ? "precall" : "postcall";

    printk(KERN_ALERT "dummyflt: dummyflt_read_iter: %s, call: %s\n", path, call);

exit:
    kfree(path);
    return REDIRFS_CONTINUE;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)

enum redirfs_rv dummyflt_permission(redirfs_context context,
        struct redirfs_args *args)
{
    char *path;
    char *call;
    int rv;

    if (!args->args.i_permission.nd)
        return REDIRFS_CONTINUE;

    path = dummyflt_alloc(sizeof(char) * PAGE_SIZE);
    if (!path)
        return REDIRFS_CONTINUE;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25))
    rv = redirfs_get_filename(args->args.i_permission.nd->mnt,
            args->args.i_permission.nd->dentry, path, PAGE_SIZE);
#else
    rv = redirfs_get_filename(args->args.i_permission.nd->path.mnt,
            args->args.i_permission.nd->path.dentry, path, PAGE_SIZE);
#endif

    if (rv) {
        printk(KERN_ERR "dummyflt: rfs_get_filename failed(%d)\n", rv);
        goto exit;
    }

    call = args->type.call == REDIRFS_PRECALL ? "precall" : "postcall";

    printk(KERN_ALERT "dummyflt: permission: %s, call: %s\n", path, call);

exit:
    kfree(path);
    return REDIRFS_CONTINUE;
}

#endif

enum redirfs_rv dummyflt_lookup(redirfs_context context,
        struct redirfs_args *args)
{
    char *path;
    char *call;
    int rv;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
    if (!args->args.i_lookup.nd)
        return REDIRFS_CONTINUE;
#endif

    path = dummyflt_alloc(sizeof(char) * PAGE_SIZE);
    if (!path)
        return REDIRFS_CONTINUE;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25))
    rv = redirfs_get_filename(args->args.i_lookup.nd->mnt,
            args->args.i_lookup.nd->dentry, path, PAGE_SIZE);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
    rv = redirfs_get_filename(args->args.i_lookup.nd->path.mnt,
            args->args.i_lookup.nd->path.dentry, path, PAGE_SIZE);
#else
    //
    // it is not possible to reliably get vfsmount from a dentry or inode
    // as dentry might be mapped at multiple paths,
    // see https://unix.stackexchange.com/questions/198590/what-is-a-bind-mount
    rv = redirfs_get_filename(NULL,
            args->args.i_lookup.dentry, path, PAGE_SIZE);
#endif

    if (rv) {
        printk(KERN_ERR "dummyflt: rfs_get_filename failed(%d)\n", rv);
        goto exit;
    }

    call = args->type.call == REDIRFS_PRECALL ? "precall" : "postcall";

    printk(KERN_ALERT "dummyflt: lookup: %s, dentry: %s, call: %s\n", path,
            call, args->args.i_lookup.dentry->d_name.name);

exit:
    kfree(path);
    return REDIRFS_CONTINUE;
}

static struct redirfs_op_info dummyflt_op_info[] = {
    {REDIRFS_REG_FOP_OPEN, dummyflt_open, dummyflt_open},
    {REDIRFS_CHR_FOP_OPEN, dummyflt_open, dummyflt_open},
    {REDIRFS_DIR_FOP_OPEN, dummyflt_open, dummyflt_open},
    {REDIRFS_REG_FOP_RELEASE, dummyflt_release, dummyflt_release},
    {REDIRFS_DIR_FOP_RELEASE, dummyflt_release, dummyflt_release},
    {REDIRFS_CHR_FOP_RELEASE, dummyflt_release, dummyflt_release},
    {REDIRFS_REG_FOP_READ, dummyflt_read, dummyflt_read},
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,0))
    {REDIRFS_REG_FOP_READ_ITER, dummyflt_read_iter, dummyflt_read_iter},
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
    {REDIRFS_REG_IOP_PERMISSION, dummyflt_permission, dummyflt_permission},
    {REDIRFS_DIR_IOP_PERMISSION, dummyflt_permission, dummyflt_permission},
#endif
    {REDIRFS_DIR_IOP_LOOKUP, dummyflt_lookup, dummyflt_lookup},
    {REDIRFS_REG_AOP_READPAGE, dummyflt_readpage, dummyflt_readpage},
    {REDIRFS_REG_AOP_READPAGES, dummyflt_readpages, dummyflt_readpages},
    {REDIRFS_OP_END, NULL, NULL}
};

//
// to facilitate with module initialization debugging
// put dummyflt_init in the .text segment
//
static int dummyflt_init(void)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
    struct nameidata nd;
#else
    struct path spath;
#endif
    redirfs_path path;

    int err;
    int rv;

    dummyflt = redirfs_register_filter(&dummyflt_info);
    if (IS_ERR(dummyflt)) {
        rv = PTR_ERR(dummyflt);
        printk(KERN_ERR "dummyflt: register filter failed(%d)\n", rv);
        return rv;
    }

    rv = redirfs_set_operations(dummyflt, dummyflt_op_info);
    if (rv) {
        printk(KERN_ERR "dummyflt: set operations failed(%d)\n", rv);
        goto error;
    }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
    rv = path_lookup("/tmp", LOOKUP_FOLLOW, &nd);
#else
    rv = kern_path("/", LOOKUP_FOLLOW, &spath);
#endif
    if (rv) {
        printk(KERN_ERR "dummyflt: path lookup failed(%d)\n", rv);
        goto error;
    }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
    dummyflt_path_info.dentry = nd.path.dentry;
    dummyflt_path_info.mnt  = nd.path.mnt;
    dummyflt_path_info.flags  = REDIRFS_PATH_INCLUDE;
#else
    dummyflt_path_info.dentry = spath.dentry;
    dummyflt_path_info.mnt  = spath.mnt;
    dummyflt_path_info.flags  = REDIRFS_PATH_INCLUDE;
#endif

    path = redirfs_add_path(dummyflt, &dummyflt_path_info);
    if (IS_ERR(path)) {
        rv = PTR_ERR(path);
        printk(KERN_ERR "dummyflt: redirfs_set_path failed(%d)\n", rv);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
        path_put(&nd.path);
#else
        path_put(&spath);
#endif
        goto error;
    }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
    path_put(&nd.path);
#else
    path_put(&spath);
#endif
    redirfs_put_path(path);

    printk(KERN_INFO "Dummy Filter Version "
            DUMMYFLT_VERSION " <www.redirfs.org>\n");
    return 0;
error:
    err = redirfs_unregister_filter(dummyflt);
    if (err) {
        printk(KERN_ERR "dummyflt: unregister filter "
                "failed(%d)\n", err);
        return 0;
    }
    redirfs_delete_filter(dummyflt);
    return rv;
}

static void __exit dummyflt_exit(void)
{
    int i = 0, ret = 0;

    redirfs_path *dummyflt_path = redirfs_get_paths(dummyflt);
    for (i = 0; i < ((struct rfs_flt*)(dummyflt))->paths_nr; i++) {
        ret = redirfs_rem_path(dummyflt, dummyflt_path[i]);
    }
    redirfs_put_paths(dummyflt_path);

    if(redirfs_unregister_filter(dummyflt) < 0) {
        mdelay(100);
        redirfs_unregister_filter(dummyflt);
    }

    redirfs_delete_filter(dummyflt);
}

module_init(dummyflt_init);
module_exit(dummyflt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Frantisek Hrbata <frantisek.hrbata@redirfs.org>");
MODULE_DESCRIPTION("Dummy Filter Version " DUMMYFLT_VERSION "<www.redirfs.org>");

