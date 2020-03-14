#include "fs.h"
#include "ide.h"
#include "memory.h"
#include "stdio_kernel.h"
#include "inode.h"
#include "dir.h"
#include "debug.h"


/* 格式化分区，也就是初始化分区的元信息，创建文件系统 */
static void partition_format(partition* part)
{
    printk("format start \n");

    uint32_t boot_sector_sects = 1;
    uint32_t super_block_sects = 1;

    /* i结点位图占用的扇区数 */
    uint32_t inode_bitmap_sects = 
            DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);

    /* inode table 占用的扇区数 */
    uint32_t inode_table_sects = 
            DIV_ROUND_UP(sizeof(struct inode) * MAX_FILES_PER_PART,
            SECTOR_SIZE);

    uint32_t used_sects = boot_sector_sects + super_block_sects +
            inode_bitmap_sects + inode_table_sects;

    uint32_t free_sects = part->sec_cnt - used_sects;


    /**********************************************************************
     * 简单处理块位图占据的扇区数   
     * 空闲位图占据的空间和空闲扇区相互依赖
     */ 
    uint32_t block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;

    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    /***********************************************************************/

    printk("  setting super block \n  ");

    super_block sb;
    sb.magic = 0x19590318;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;

    /* 第0块是引导块，第1块是超级快, 跨过了两个扇区 */
    sb.block_bitmap_lba = sb.part_lba_base + 2;
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + 
            sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;   

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;

    sb.root_inode_no = 0;

    sb.dir_entry_size = sizeof(dir_entry);

    disk* hd = part->my_disk;
    
    /* 1 把超级块写入本分区的1扇区  */
    ide_write(hd, part->start_lba + 1, &sb, 1);

    printk(" setting super block done\n");

    /* 找出最大元信息 */
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ?
        sb.block_bitmap_sects : sb.inode_bitmap_sects) * SECTOR_SIZE;
    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    /* 已经请0， 用来存储位图 */
    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);

    /* 2 把块位图初始化并写入sb.block_bitmap_lba              */

    /* 第0块预留给根目录 */
    buf[0] |= 0x01;
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
    uint32_t last_size = SECTOR_SIZE - 
             (block_bitmap_last_byte % SECTOR_SIZE);
    
    /* 先将位图最后一字节到所在扇区的结束变量设置为1 */
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);

    uint8_t bit_idx = 0;
    /* 还能用的扇区重新设置为0 */
    while(bit_idx <= block_bitmap_last_bit)
    {
        buf[block_bitmap_last_byte] &= (0 << bit_idx++);
    }
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    printk("write block bitmap done\n");

    /* 3 将inode位图初始化并写入sb.inode_bitmap_lba */
    memset(buf, 0, buf_size);

    /* 第0个inode分配给根结点 */
    buf[0] |= 0x01;

    /**************************************************************
     * inode_table 中共4096个inode
     * 位图inode_bitmap中正好占用一个扇区
     * 即inode_bitmap_sects 等于1，所以不需要特殊处理
     */ 
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    printk("   write inode bitmap done \n ");

    /***************************************************************
     * 4 将inode数组初始化并写入sb.inode_table_lba */
    printk("buf address at 0x%x\n", (uint32_t)buf);
    printk("buf end at 0x%x\n", (uint32_t)buf + buf_size);
    memset(buf, 0, buf_size);
    struct inode* i = (struct inode*)buf;

    /* 目录 . 和 ..*/
    i->i_size = sb.dir_entry_size * 2;

    /* 根目录占inode数组中第0个inode */
    i->i_no = 0;

    /* 第一个扇区已经初始化为0 */
    i->i_sectors[0] = sb.data_start_lba;

    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);


    /* 5 根目录写入sb.data_start_lba                          */
    memset(buf, 0, buf_size);
    dir_entry* p_de = (dir_entry*)buf;

    /* 初始化当前目录 */
    memcpy(p_de->filename, ".", 1);
    p_de->f_type = FT_DIRECTORY;
    p_de++;

    /* 初始化当前目录的父目录 */
    memcpy(p_de->filename, "..", 2);

    /* 根目录的父目录依然是根目录自己 */
    p_de->i_no = 0;

    p_de->f_type = FT_DIRECTORY;

    /* sb.block_start_lba 已经分配给了根目录，里面是根目录的目录项 */
    ide_write(hd, sb.data_start_lba, buf, 1);

    printk("   format done\n   ");

    sys_free(buf);
}

/* 在分区链表中找到名为part_name的分区，并将指针赋值给cur_part */
static bool mount_partition(struct list_elem* pelem, int arg)
{
    char* part_name = (char*)arg;
    partition* part = elem2entry(struct partition, part_tag, pelem);
        
    printk("%s len %d \n", part->name, strlen(part->name));
    printk("%s len %d \n", part_name, strlen(part_name));

    /* 如果名字相等 */
    if(!strcmp(part->name, part_name))
    {
        cur_part = part;
        struct disk* hd = cur_part->my_disk;

        /* sb_buf用来存储从磁盘上读入的超级块 */
        struct super_block* sb_buf = 
            (struct super_block*)sys_malloc(SECTOR_SIZE);
        
        /* 在内存中创建分区cur_part的超级块 */
        cur_part->sb = (struct super_block*)sys_malloc(sizeof(super_block));
        if(cur_part->sb == NULL)
        {
            PANIC("alloc memory failed!\n");
        }

        /* 读入超级块 */
        //memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);
        

        /* 把sb_buf中超级块的信息复制到分区的超级块sb中 */
        memcpy(cur_part->sb, sb_buf, sizeof(super_block));

        /* 超级块大小不足一个扇区 */
        sys_free(sb_buf);

        /***** 把硬盘上的块位图读入内存   **********/
        cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(
            sb_buf->block_bitmap_sects * SECTOR_SIZE
        );

        if(cur_part->block_bitmap.bits == NULL)
        {
            PANIC("alloc memory failed");
        }
        cur_part->block_bitmap.btmp_bytes_len = 
            sb_buf->block_bitmap_sects * SECTOR_SIZE;

        /* 从硬盘上读入块位图到分区的block_bitmap_bits */
        ide_read(hd, sb_buf->block_bitmap_lba, 
            cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);
        
        /**** 把磁盘上的inode位图读入内存  ********************/
        cur_part->inode_bitmap.bits = 
            (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if(cur_part->inode_bitmap.bits == NULL)
        {
            PANIC("alloc memory failed!\n");
        }
        cur_part->inode_bitmap.btmp_bytes_len = 
            sb_buf->inode_bitmap_sects * SECTOR_SIZE;
        
        /***  从硬盘上读入inode位图到分区的inode_bitmap_bits  ***/
        ide_read(hd, sb_buf->inode_bitmap_lba,
            cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);
        /***************************************************************/

        list_init(&cur_part->open_inodes);
        printk("mount %s done!\n", part->name);
        return true;
    }
    printk("mount %s failed\n", part_name);
    return false;
}

/* 在磁盘上搜索文件系统，若没有则格式化分区创建文件系统 */
void filesys_init()
{
    uint8_t channel_no = 0;
    uint8_t dev_no = 0;
    uint8_t part_idx = 0;

    /* sb_buf 存放从磁盘上读取的超级块 */
    super_block* sb_buf = (super_block*)sys_malloc(SECTOR_SIZE);

    if(sb_buf == NULL)
    {
        PANIC("alloc memory failed!");
    }
    printk("searching file systems....\n");
    while(channel_no < channel_cnt)
    {
        dev_no = 0;
        while(dev_no < 2)
        {
            if(dev_no == 0) // 跨过磁盘hd60M.img
            {
                dev_no++;
                continue;
            }
            disk* hd = &channels[channel_no].devices[dev_no];
            partition* part = hd->prim_parts;
            while(part_idx < 4 + 8)
            {
                if(part_idx == 4)
                {
                    part = hd->logic_parts;
                }
                
                if(part->sec_cnt != 0 )
                {
                    memset(sb_buf, 0, SECTOR_SIZE);

                    /* 读出分区的超级块，根据魔数是否正确来判断是否存在文件系统 */
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);

                    /* 已有文件系统 */
                    if(sb_buf->magic == 0x19590318)
                    {
                        printk("%s has file system\n", part->name);
                    }
                    else
                    {
                        printk("formatting %s's partition %s....\n",
                            hd->name, part->name);

                        partition_format(part);
                    }
                    
                }
                part_idx++;
                part++;  // 下一分区
            }
            dev_no++;    // 下一磁盘
        }
        channel_no++;    // 下一通道
    }
    sys_free(sb_buf);

    /* 确定默认操作的分区 */
    char default_part[] = "sdb1";

    /* 挂载分区 */
    list_traversal(&partition_list, mount_partition, (int)default_part);
}

/* 把最上层的路径名字解析出来 */
static char* path_parse(char* pathname, char* name_store)
{
    int i = 0;
    /* 根目录不需要解析 */
    if(pathname[0] == '/')
    {
        for(; pathname[i] == '/'; i++);
    }
    
    /* 一般名字解析 */
    for(; pathname[i] != '/' && pathname[i] != '\0'; i++)
    {
        *name_store++ = pathname[i];
    }

    if(pathname[i] == '\0')
    {
        return NULL;
    }

    /* 返回下一个路径的子字符串 */
    return pathname + i;
}

/* 返回路径深度，比如/a/b/c 深度为3 */
int32_t path_depth_cnt(char* pathname)
{
    ASSERT(pathname != NULL);
    char* p = pathname;
    char name[MAX_FILE_NAME_LEN];
    uint32_t depth = 0;
    for(; p != NULL; p = path_parse(p, name))
    {
        depth++;
    }
    return depth;
}