/*
 * (C) Copyright 2010 Quantenna Communications Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/hardware.h>

#include <asm/board/platform.h>
#include <asm/board/gpio.h>
#include <asm/board/board_config.h>
#include <asm/errno.h>

#include <common/ruby_spi_api.h>
#include <common/ruby_spi_flash_data.h>
#include <common/ruby_partitions.h>

#include "spi_api.h"

/* Driver name */
#define SPI_FLASH_DRIVER		"spi_flash"

/* Swap bytes */
#define SWAP32(x)			((((x) & 0x000000ff) << 24)  | \
					(((x)  & 0x0000ff00) << 8)   | \
					(((x)  & 0x00ff0000) >> 8)   | \
					(((x)  & 0xff000000) >> 24))

/* Timeout */
#define SPI_READY_TIMEOUT_MS		10000

/* Each flash chip has same page size */
#define SPI_PAGE_SIZE			256


/* Set zero if want to disable log messages */
#define SPI_FLASH_DEBUG			0

#if SPI_FLASH_DEBUG
	#define SPI_LOG(a...)		printk(a)
#else
	#define SPI_LOG(a...)
#endif

/* Structure which holds all allocated for SPI flash resources.
*
* Access to SPI controller must be only when hold 'lock' mutex.
*
* To read data cached ioremapped 'mem_read_cache' is used.
* Why cached - because ARC processor works more efficient
* with AMBA bus when access comes through d-cache.
*
* To write data NON-cached ioremapped 'mem_write_nocache' is used.
* Why non-cached - because in Linux process (if not be extra-cautious)
* can be interrupted or switched very easily when half cache-line is
* updated only. This half-updated cache-line can be flushed (because
* it is dirty). Flush means 'write'. So half-updated data will be stored
* on flash. It is not a problem in case of reading (see cached 'mem_read_cache')
* as after cache-line invalidating FAST_READ SPI command will be issued again
* and we will have only slight performance penalty. And it is real problem
* when do writing. So for writing let's have non-cached memory area.
* Disadvantage - we use twice more virtual memory (should not be problem on Ruby).
*/
struct spi_flash
{
	struct mutex			lock;
	struct mtd_info			mtd;
	void				*mem_write_nocache;
	const void			*mem_read_cache;
	struct flash_info		*info;
	unsigned			partitioned;
};

static inline struct spi_flash* mtd_to_flash(struct mtd_info *mtd)
{
	return container_of(mtd, struct spi_flash, mtd);
}

static inline void spi_ctrl_clock_config(u32 val)
{
	writel(RUBY_SYS_CTL_MASK_SPICLK, IO_ADDRESS(RUBY_SYS_CTL_MASK));
	writel(RUBY_SYS_CTL_SPICLK(val), IO_ADDRESS(RUBY_SYS_CTL_CTRL));
	writel(0x0, IO_ADDRESS(RUBY_SYS_CTL_MASK));
}

static inline void spi_clock_config(unsigned long freq)
{
	const unsigned long ref = CONFIG_ARC700_DEV_CLK;

	if (freq >= (ref / 2)) {
		spi_ctrl_clock_config(0x0);
	} else if (freq >= (ref / 4)) {
		spi_ctrl_clock_config(0x1);
	} else if(freq >= (ref / 8)) {
		spi_ctrl_clock_config(0x2);
	} else {
		spi_ctrl_clock_config(0x3);
	}
}

static inline size_t spi_flash_size(const struct flash_info *info)
{
	return (info->sector_size * info->n_sectors);
}

static inline int spi_flash_sector_aligned(const struct flash_info *info, loff_t addr)
{
	return !(addr % info->sector_size);
}

static inline u32 spi_flash_id(void)
{
	return (SWAP32(readl(IO_ADDRESS(RUBY_SPI_READ_ID))) & 0xFFFFFF);
}

static inline void spi_flash_deassert_cs(void)
{
	spi_flash_id();
}

void spi_flash_write_enable(void)
{
	writel(0, IO_ADDRESS(RUBY_SPI_WRITE_EN));
}

static inline u32 spi_flash_status(void)
{
	return (SWAP32(readl(IO_ADDRESS(RUBY_SPI_READ_STATUS))) & 0xFF);
}

static inline int spi_flash_ready(void)
{
	return !(spi_flash_status() & RUBY_SPI_WR_IN_PROGRESS);
}

int spi_flash_wait_ready(const struct flash_info *info)
{
	int ret = -ETIMEDOUT;
	const unsigned long deadline = jiffies +
		max((unsigned long)(SPI_READY_TIMEOUT_MS * HZ / 1000 * info->n_sectors), 1UL);
	unsigned long counter = max((unsigned long)info->n_sectors * SPI_READY_TIMEOUT_MS, 1UL);

	do {
		if(spi_flash_ready()) {
			ret = 0;
			break;
		}

		if(counter) {
			--counter;
		}

		cond_resched();

	} while (!time_after_eq(jiffies, deadline) || counter);

	return ret;
}

static inline u32 spi_flash_addr(loff_t addr)
{
	return SPI_MEM_ADDR(addr);
}

static inline unsigned long spi_flash_align_begin(unsigned long addr, unsigned long step)
{
	return (addr & (~(step - 1)));
}

static inline unsigned long spi_flash_align_end(unsigned long addr, unsigned long step)
{
	return ((addr + step - 1) & (~(step - 1)));
}

static inline void spi_flash_cache_inv(unsigned long begin, unsigned long end)
{
	/* ARC Cache uses physical addresses */
	inv_dcache_range(
		spi_flash_align_begin(RUBY_SPI_FLASH_ADDR + begin, ARC_DCACHE_LINE_LEN),
		spi_flash_align_end(RUBY_SPI_FLASH_ADDR + end, ARC_DCACHE_LINE_LEN));
}

static inline void* spi_flash_ioremap(struct spi_flash *flash, int cache)
{
	void *ret = NULL;
	const unsigned long begin = spi_flash_align_begin(RUBY_SPI_FLASH_ADDR, PAGE_SIZE);
	const unsigned long end = spi_flash_align_end(RUBY_SPI_FLASH_ADDR + spi_flash_size(flash->info), PAGE_SIZE);

	if (cache) {
		ret = ioremap(begin, end - begin);
	} else {
		ret = ioremap_nocache(begin, end - begin);
	}

	return ret;
}

static int spi_flash_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret = 0;
	struct spi_flash *flash = mtd_to_flash(mtd);
	loff_t erase_begin = instr->addr;
	loff_t erase_end = min(erase_begin + (loff_t)instr->len, (loff_t)mtd->size);

	SPI_LOG(KERN_INFO"%s: erase: begin=0x%x end=0x%x\n", SPI_FLASH_DRIVER, (unsigned)erase_begin, (unsigned)erase_end);

	/* Pre-condition. */
	if (erase_begin >= erase_end || !(spi_flash_sector_aligned(flash->info, erase_begin) &&
			spi_flash_sector_aligned(flash->info, erase_end))) {

		/* Request is out of range or not alligned.
		 * Although it is legal to have erase address
		 * inside sector, it is not very safe.
		 * Simple mistake - and neighbour sector erased.
		 */
		ret = -ERANGE;
		SPI_LOG(KERN_ERR"%s: erase: failed: ret=%d\n", SPI_FLASH_DRIVER, ret);
		return ret;

	}

	/* Begin synchronized block */
	mutex_lock(&flash->lock);

	/* Going to bypass d-cache, so invalidate it before. */
	spi_flash_cache_inv(erase_begin, erase_end);

	/* Erasing */
	if ((erase_begin == 0) && (erase_end == mtd->size)) {

		ret = spi_unprotect_all(flash->info);
		if (ret) {
			mutex_unlock(&flash->lock);
			printk(KERN_ERR "%s:Failed to unprotect all regions of the Flash\n", SPI_FLASH_DRIVER);
			return (ret);
		}

		/* Bulk erase */

		ret = spi_flash_wait_ready(flash->info);
		if (!ret) {
			spi_flash_write_enable();
			writel(0, IO_ADDRESS(RUBY_SPI_BULK_ERASE));
		}

	} else {
		while (erase_begin < erase_end) {


			ret = spi_flash_wait_ready(flash->info);
			if (ret) {
				break;
			}

			/* Per-sector erase */
			if (spi_device_erase(flash->info, erase_begin)  < 0){
				break;
			}

			erase_begin += flash->info->sector_size;
		}
	}

	if (ret) {
		instr->state = MTD_ERASE_FAILED;
		SPI_LOG(KERN_ERR"%s: erase: failed: ret=%d\n", SPI_FLASH_DRIVER, ret);
	} else {
		instr->state = MTD_ERASE_DONE;
		SPI_LOG(KERN_INFO"%s: erase: succeed\n", SPI_FLASH_DRIVER);
	}

	spi_flash_deassert_cs();
	mtd_erase_callback(instr);

	spi_flash_wait_ready(flash->info);
	ret = spi_protect_all(flash->info);

	/* End synchronized block */
	mutex_unlock(&flash->lock);

	if (ret) {
		printk(KERN_ERR"%s: Failed to protect all regions of the Flash\n", SPI_FLASH_DRIVER);
	}
	return ret;
}

static int spi_flash_read(struct mtd_info *mtd, loff_t read_begin, size_t len, size_t *ret_len, u_char *buf)
{
	int ret = 0;
	struct spi_flash *flash = mtd_to_flash(mtd);
	loff_t read_end = min(read_begin + (loff_t)len, (loff_t)mtd->size);
	size_t read_len = 0;

	SPI_LOG(KERN_INFO"%s: read: begin=0x%x len=%u\n", SPI_FLASH_DRIVER, (unsigned)read_begin, (unsigned)len);

	/* Pre-condition. */
	if(read_begin >= read_end) {
		/* Request is out of range. */
		ret = -ERANGE;
		SPI_LOG(KERN_ERR"%s: read: failed: ret=%d\n", SPI_FLASH_DRIVER, ret);
		return ret;
	}

	/* Calculate read length */
	read_len = read_end - read_begin;

	/* Begin synchronized block */
	mutex_lock(&flash->lock);

	/* Reading */
	ret = spi_flash_wait_ready(flash->info);
	if (!ret) {
#ifdef CONFIG_PREEMPT
		memcpy(buf, (u_char *)flash->mem_read_cache + read_begin, read_len);
#else
		while (1) {

			size_t iter_read_len = min((size_t)(read_end - read_begin), flash->info->sector_size);

			memcpy(buf, (u_char *)flash->mem_read_cache + read_begin, iter_read_len);

			read_begin += iter_read_len;
			buf += iter_read_len;
			if (read_begin == read_end) {
				break;
			}

			cond_resched();
		}
#endif // #ifdef CONFIG_PREEMPT
	}

	spi_flash_deassert_cs();

	/* End synchronized block */
	mutex_unlock(&flash->lock);

	if (ret) {
		SPI_LOG(KERN_ERR"%s: read: failed: ret=%d\n", SPI_FLASH_DRIVER, ret);
	} else {
		*ret_len = read_len;
		SPI_LOG(KERN_INFO"%s: read: succeed: len=%u\n", SPI_FLASH_DRIVER, (unsigned)*ret_len);
	}

	return ret;
}

static int spi_flash_write(struct mtd_info *mtd, loff_t write_begin, size_t len, size_t *ret_len, const u_char *buf)
{
	int ret = 0;
	struct spi_flash *flash = mtd_to_flash(mtd);
	loff_t write_end = min(write_begin + len, (loff_t)mtd->size);
	size_t write_len = 0;
	int i;

	SPI_LOG(KERN_INFO"%s: write: begin=0x%x len=%u\n", SPI_FLASH_DRIVER, (unsigned)write_begin, (unsigned)len);
	/* Pre-condition. */
	if (write_begin >= write_end) {
		/* Request is out of range. */
		ret = -ERANGE;
		SPI_LOG(KERN_ERR"%s: write: failed: ret=%d\n", SPI_FLASH_DRIVER, ret);
		return ret;
	}

	/* Calculate write length */
	write_len = write_end - write_begin;

	/* Begin synchronized block */
	mutex_lock(&flash->lock);

	/* Going to bypass d-cache, so invalidate it before. */
	spi_flash_cache_inv(write_begin, write_end);

	/* Writing */
	while (write_begin < write_end) {

		/* Per-page programming */

		u32 iter_write_len = min(
			SPI_PAGE_SIZE - (write_begin % SPI_PAGE_SIZE), /* do not exceed page boundary */
			write_end - write_begin); /* do not exceed requested range */

		ret = spi_flash_wait_ready(flash->info);
		if (ret) {
			break;
		}

		ret = spi_unprotect_sector(flash->info, write_begin);
		if (ret) {
			printk(KERN_ERR"%s: Failed to unprotect Sector %x \n", SPI_FLASH_DRIVER,
				(unsigned int)write_begin);
			break;
		}
		spi_flash_write_enable();
		if ( flash->info->sector_size * flash->info->n_sectors > RUBY_SPI_BOUNDARY_4B ){
			writel(SPI_MEM_ADDR_4B(write_begin), IO_ADDRESS(RUBY_SPI_PAGE_PROGRAM_4B));
		} else {
			writel(spi_flash_addr(write_begin), IO_ADDRESS(RUBY_SPI_PAGE_PROGRAM));
		}

		/*
		 * memcpy((u_char *)flash->mem_write_nocache + write_begin, buf, iter_write_len);
		 * memcpy doesn't work correctly here for Linux_2.6.35.12, due to implementation of it.
		 */
		for (i = 0; i < iter_write_len; i++)
			*((u_char *)flash->mem_write_nocache + write_begin + i) = *(buf + i);
		
		writel(0, IO_ADDRESS(RUBY_SPI_COMMIT));

		write_begin += iter_write_len;
		buf += iter_write_len;
	}

	if (ret) {
		SPI_LOG(KERN_ERR"%s: write: failed: ret=%d\n", SPI_FLASH_DRIVER, ret);
	} else {
		*ret_len = write_len;
		SPI_LOG(KERN_INFO"%s: write: succeed: len=%u\n", SPI_FLASH_DRIVER, (unsigned)*ret_len);
	}

	spi_flash_deassert_cs();

	spi_flash_wait_ready(flash->info);
	ret = spi_protect_all(flash->info);

	/* End synchronized block */
	mutex_unlock(&flash->lock);

	if (ret) {
		printk(KERN_ERR"%s: Failed to protect all regions of the Flash \n", SPI_FLASH_DRIVER);
	}

	return ret;
}

static void spi_flash_sync(struct mtd_info *mtd)
{
	struct spi_flash *flash = mtd_to_flash(mtd);

	SPI_LOG(KERN_INFO"%s: sync: begin\n", SPI_FLASH_DRIVER);

	/* Begin synchronized block */
	mutex_lock(&flash->lock);

	/* Make sure that all pending write/erase transactions are finished */
	(void)spi_flash_wait_ready(flash->info);

	/* End synchronized block */
	mutex_unlock(&flash->lock);

	SPI_LOG(KERN_INFO"%s: sync: end\n", SPI_FLASH_DRIVER);
}

static struct flash_info* __init spi_flash_info(void)
{
	u32 jedec_id = spi_flash_id();
	int i;
	int flash_size = 0;

	for (i = 0; i < ARRAY_SIZE(flash_data); ++i) {
		if (jedec_id == flash_data[i].jedec_id) {
			return flash_data + i;
		}
	}

	printk(KERN_ERR "SPI_FLASH: unsupported jedec found: 0x%x, using generic flash\n", jedec_id);
	if (!(flash_size = qtn_get_flash_size())) {
		printk(KERN_ERR "SPI_FLASH: set flash_size={64KB|256KB|...} in env bootargs\n");
		return NULL;
	}
	jedec_id = SPI_GENERIC_MAGIC;
	for (i = 0; i < ARRAY_SIZE(flash_data); i++) {
		if (jedec_id == flash_data[i].jedec_id) {
			if (flash_size <= SPI_FLASH_SIZE_1M) {
				flash_data[i].sector_size = SPI_SECTOR_4K;
				flash_data[i].flags = SECTOR_ERASE_OP20;
			}
			if (flash_data[i].sector_size)
				flash_data[i].n_sectors = flash_size / flash_data[i].sector_size;
			return flash_data + i;
		}
	}
	return NULL;
}

static void spi_flash_dealloc(struct spi_flash *flash)
{
	if (flash) {

		if (flash->mem_read_cache) {
			iounmap(flash->mem_read_cache);
		}

		if (flash->mem_write_nocache) {
			iounmap(flash->mem_write_nocache);
		}

		kfree(flash);
	}
}

static struct spi_flash* __init spi_flash_alloc(void)
{
	/* Allocate structure to hold flash specific data. */
	struct spi_flash *flash = kzalloc(sizeof(struct spi_flash), GFP_KERNEL);
	if (!flash) {
		printk(KERN_ERR"%s: no memory\n", SPI_FLASH_DRIVER);
		goto error_exit;
	}

	/* Cannot setup proper clock yet, so set up slowest possible mode. */
	spi_clock_config(FREQ_UNKNOWN);

	/* Get flash information. */
	flash->info = spi_flash_info();
	if (!flash->info) {
		printk(KERN_ERR"%s: cannot get info\n", SPI_FLASH_DRIVER);
		goto error_exit;
	}

	/* Now we are ready to setup correct frequency. */
	spi_clock_config(flash->info->freq);

	/* Map flash memory. We need both cached and non-cached access. */
	flash->mem_read_cache = spi_flash_ioremap(flash, 1);
	flash->mem_write_nocache = spi_flash_ioremap(flash, 0);
	if (!flash->mem_read_cache || !flash->mem_write_nocache) {
		printk(KERN_ERR"%s: cannot remap IO memory\n", SPI_FLASH_DRIVER);
		goto error_exit;
	}

	/* Initialize mutex */
	mutex_init(&flash->lock);

	return flash;

error_exit:
	spi_flash_dealloc(flash);
	return NULL;
}

static void __init spi_flash_init_mtd(struct spi_flash *flash)
{
	flash->mtd.name = SPI_FLASH_DRIVER;
	flash->mtd.type = MTD_NORFLASH;
	flash->mtd.writesize = 1;
	flash->mtd.flags = MTD_CAP_NORFLASH;
	flash->mtd.size = spi_flash_size(flash->info);
	flash->mtd.erase = spi_flash_erase;
	flash->mtd.write = spi_flash_write;
	flash->mtd.read = spi_flash_read;
	flash->mtd.sync = spi_flash_sync;
	flash->mtd.erasesize = flash->info->sector_size;
}


#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,30)
#define	RUBY_MTD_PART(name, size, offset)	{name, size, offset, 0, NULL, NULL}
#else
#define	RUBY_MTD_PART(name, size, offset)	{name, size, offset, 0, NULL}
#endif

static struct mtd_partition __initdata parts_64K[] = {
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_BIN,	F64K_UBOOT_PIGGY_PARTITION_SIZE, 0),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV,	F64K_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_DATA,	MTDPART_SIZ_FULL,		MTDPART_OFS_NXTBLK),
};

#ifdef FLASH_SUPPORT_256KB
static struct mtd_partition __initdata parts_256K[] = {
        RUBY_MTD_PART(MTD_PARTNAME_UBOOT_BIN,   F256K_UBOOT_PIGGY_PARTITION_SIZE, 0),
        RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV,   F256K_ENV_PARTITION_SIZE,        MTDPART_OFS_NXTBLK),
        RUBY_MTD_PART(MTD_PARTNAME_DATA,        MTDPART_SIZ_FULL,               MTDPART_OFS_NXTBLK),
};
#endif
static struct mtd_partition __initdata parts_2M[] = {
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_BIN,	UBOOT_TEXT_PARTITION_SIZE,	0),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV,	UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV_BAK,UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_DATA,	MTDPART_SIZ_FULL,		MTDPART_OFS_NXTBLK),
};
static struct mtd_partition __initdata parts_4M[] = {
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_BIN,	UBOOT_TEXT_PARTITION_SIZE,	0),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV,	UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV_BAK,UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_DATA,	MTDPART_SIZ_FULL,		MTDPART_OFS_NXTBLK),
};
static struct mtd_partition __initdata parts_8M[] = {
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_BIN,	UBOOT_TEXT_PARTITION_SIZE,	0),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV,	UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV_BAK,UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_LINUX_LIVE,	IMG_SIZE_8M_FLASH_1_IMG,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_DATA,	MTDPART_SIZ_FULL,		MTDPART_OFS_NXTBLK),
};

static struct mtd_partition __initdata parts_16M[] = {
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_BIN,	UBOOT_TEXT_PARTITION_SIZE,	0),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV,	UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV_BAK,UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_LINUX_SAFETY,IMG_SIZE_16M_FLASH_2_IMG,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_LINUX_LIVE,	IMG_SIZE_16M_FLASH_2_IMG,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_DATA,	MTDPART_SIZ_FULL,		MTDPART_OFS_NXTBLK),
};

static struct mtd_partition __initdata parts_32M[] = {
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_BIN,	UBOOT_TEXT_PARTITION_SIZE,	0),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV,	UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV_BAK,UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
#ifdef TOPAZ_HOST_BUILD
	RUBY_MTD_PART(MTD_PARTNAME_LINUX_LIVE,	IMG_SIZE_16M_FLASH_1_IMG,	MTDPART_OFS_NXTBLK),
#else
	RUBY_MTD_PART(MTD_PARTNAME_LINUX_SAFETY,IMG_SIZE_16M_FLASH_2_IMG,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_LINUX_LIVE,	IMG_SIZE_16M_FLASH_2_IMG,	MTDPART_OFS_NXTBLK),
#endif
	RUBY_MTD_PART(MTD_PARTNAME_DATA,	RUBY_MIN_DATA_PARTITION_SIZE,		MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_EXTEND,	MTDPART_SIZ_FULL,		MTDPART_OFS_NXTBLK),
};

static struct mtd_partition __initdata parts_64M[] = {
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_BIN,	UBOOT_TEXT_PARTITION_SIZE,	0),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV,	UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_UBOOT_ENV_BAK,UBOOT_ENV_PARTITION_SIZE,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_LINUX_SAFETY,IMG_SIZE_16M_FLASH_2_IMG,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_LINUX_LIVE,	IMG_SIZE_16M_FLASH_2_IMG,	MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_DATA,	RUBY_MIN_DATA_PARTITION_SIZE,		MTDPART_OFS_NXTBLK),
	RUBY_MTD_PART(MTD_PARTNAME_EXTEND,	MTDPART_SIZ_FULL,		MTDPART_OFS_NXTBLK),
};

struct mtd_partition_table {
	int flashsz;
	struct mtd_partition *parts;
	unsigned int nparts;
};

static const struct mtd_partition_table partition_tables[] = {
	{ FLASH_64MB,	parts_64M,	ARRAY_SIZE(parts_64M)	},
	{ FLASH_32MB,	parts_32M,	ARRAY_SIZE(parts_32M)	},
	{ FLASH_16MB,	parts_16M,	ARRAY_SIZE(parts_16M)	},
	{ FLASH_8MB,	parts_8M,	ARRAY_SIZE(parts_8M)	},
	{ FLASH_4MB,	parts_4M,	ARRAY_SIZE(parts_4M)	},
	{ FLASH_2MB,	parts_2M,	ARRAY_SIZE(parts_2M)	},
#ifdef FLASH_SUPPORT_256KB
        { FLASH_256KB,  parts_256K,     ARRAY_SIZE(parts_256K)   },
#endif
	{ FLASH_64KB,	parts_64K,	ARRAY_SIZE(parts_64K)	},
};

static int __init spi_flash_add_mtd(struct spi_flash *flash)
{
	spi_flash_init_mtd(flash);

	if (mtd_has_partitions()) {
		struct mtd_partition *parts;
		int nr_parts = 0;
		int cfg_flash_sz = 0;
		int jedec_flash_sz = 0;
		int flashsz = 0;

		// prioritise paritions being sent as command line parameters
		if (mtd_has_cmdlinepart()) {
			static const char *part_probes[]
				= { "cmdlinepart", NULL };

			nr_parts = parse_mtd_partitions(&flash->mtd,
					part_probes, &parts, 0);
		}

		// provide a default table if command line parameter fails
		if (nr_parts <= 0) {
			int i;

			get_board_config(BOARD_CFG_FLASH_SIZE, &cfg_flash_sz);
			jedec_flash_sz = spi_flash_size(flash->info);
			if (cfg_flash_sz) {
				flashsz = min(cfg_flash_sz, jedec_flash_sz);
			} else {
				flashsz = jedec_flash_sz;
			}

			for (i = 0; i < ARRAY_SIZE(partition_tables); i++) {
				const struct mtd_partition_table *t;

				t = &partition_tables[i];
				if (flashsz == t->flashsz) {
					nr_parts = t->nparts;
					parts = t->parts;
					break;
				}
			}
		}

		if (nr_parts > 0) {
			flash->partitioned = 1;
			return add_mtd_partitions(&flash->mtd, parts, nr_parts);
		} else {
			panic("%s: No valid flash partition table."
					" flashsz board/jedec/sel = 0x%x/0x%x/0x%x\n",
					SPI_FLASH_DRIVER, cfg_flash_sz, jedec_flash_sz, flashsz);
		}
	}

	return add_mtd_device(&flash->mtd);
}

static int __init spi_flash_attach(struct spi_flash **flash_ret)
{
	int ret = 0;

	struct spi_flash *flash = spi_flash_alloc();
	if (!flash) {
		printk(KERN_ERR"%s: allocation failed\n", SPI_FLASH_DRIVER);
		ret = -ENOMEM;
		goto error;
	}

	if ( flash->info->sector_size * flash->info->n_sectors > RUBY_SPI_BOUNDARY_4B ){
		writel(RUBY_SPI_ADDRESS_MODE_4B, RUBY_SPI_CONTROL);
	}

	*flash_ret = flash;

	/* If flag is set, enable support Protection mode */

	if ((qtn_get_spi_protect_config() & 0x1)){
		if (spi_protect_mode(flash->info) == EOPNOTSUPP){
			printk(KERN_INFO "%s: SPI Protected Mode is not Supported\n", SPI_FLASH_DRIVER);
			flash->info->single_unprotect_mode = NOT_SUPPORTED;
		} else {
			printk(KERN_INFO "%s: SPI Protected Mode is Supported\n", SPI_FLASH_DRIVER);
		}
	} else {
		if (spi_read_scur() & SPI_SCUR_WPSEL) {
			printk(KERN_INFO "SPI Device Is In Protected Mode\n");
		} else {
			/* No Protection */
			printk(KERN_INFO "%s: Force not to support Protect Mode \n", SPI_FLASH_DRIVER);
			flash->info->single_unprotect_mode = NOT_SUPPORTED;
		}
	}

	ret = spi_flash_add_mtd(flash);
	if (ret) {
		printk(KERN_ERR"%s: MTD registering failed\n", SPI_FLASH_DRIVER);
		goto error;
	}

	return ret;

error:
	spi_flash_dealloc(flash);
	return ret;
}

static void __exit spi_flash_deattach(struct spi_flash *flash)
{
	int status = 0;

	if (!flash) {
		return;
	}

	if (mtd_has_partitions() && flash->partitioned) {
		status = del_mtd_partitions(&flash->mtd);
	} else {
		status = del_mtd_device(&flash->mtd);
	}

	if (status) {
		printk(KERN_ERR"%s: cannot delete MTD\n", SPI_FLASH_DRIVER);
	} else {
		spi_flash_dealloc(flash);
	}
}

/* We can have only single flash chip connected to SPI controller */
static struct spi_flash *g_flash = NULL;


/******************************************************************************
* Function:spi_api_flash_status
* Purpose: read status of te Flash
* Returns: TIMEOUT or success
* Note:
* ***********************************************************************************/
int spi_api_flash_status(void)
{
	int ret = -1;

	if (g_flash){
		ret = spi_flash_wait_ready(g_flash->info);
	}

	return (ret);
}

/* For external use to query flash size */
size_t get_flash_size(void)
{
	if (g_flash)
		return g_flash->mtd.size;
	else
		return 0;
}
EXPORT_SYMBOL(get_flash_size);

uint32_t spi_read_one( unsigned int cmd)
{
	int spi_cmd, val;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;
	val = SWAP32(readl(spi_cmd))& RUBY_SPI_READ_DATA_MASK;
	return val;
}

uint32_t spi_read_two( unsigned int cmd)
{
	int spi_cmd, val;
	uint32_t spi_ctrl_val;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_TWO_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(0, spi_cmd);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	val = readw(RUBY_SPI_COMMIT);
	return val;
}

uint32_t spi_read_four( unsigned int cmd)
{
	int spi_cmd, val;
	uint32_t spi_ctrl_val;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_FOUR_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(0, spi_cmd);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	val = readl(RUBY_SPI_COMMIT);
	return val;
}


uint32_t spi_read_one_addr( unsigned int cmd, unsigned int addr)
{
	int spi_cmd, val;
	uint32_t spi_ctrl_val;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_ONE_ADDR_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(SPI_MEM_ADDR_4B(addr), spi_cmd);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	val = readb(RUBY_SPI_COMMIT);
	return val;
}


int spi_write_cmd(unsigned int cmd)
{
	uint32_t spi_ctrl_val;
	int spi_cmd;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_CMD_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(0, spi_cmd);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	return 0;
}


int spi_write_one(unsigned int cmd, uint32_t value)
{
	uint32_t spi_ctrl_val;
	int spi_cmd;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_ONE_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(SWAP32(value), spi_cmd);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	return 0;
}

int spi_write_two(unsigned int cmd, uint32_t value)
{
	uint32_t spi_ctrl_val;
	int spi_cmd;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_TWO_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(SWAP32(value), spi_cmd);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	return 0;
}

int spi_write_four(unsigned int cmd, uint32_t value)
{
	uint32_t spi_ctrl_val;
	int spi_cmd;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_FOUR_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(SWAP32(value), spi_cmd);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	return 0;
}

int spi_write_four_prefix_4null(unsigned int cmd, uint32_t value)
{
	uint32_t spi_ctrl_val;
	int spi_cmd;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_EIGHT_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(SWAP32(value), spi_cmd);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	return 0;
}



int spi_write_addr(unsigned int cmd, unsigned int addr)
{
	int spi_cmd;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	writel(SPI_MEM_ADDR_4B(addr), spi_cmd);

	return 0;

}

uint32_t spi_mxic_read_scur(void)
{
	return spi_read_one(RUBY_SPI_READ_SCUR);
}

uint32_t spi_mxic_read_lock(void)
{
	return spi_read_two(RUBY_SPI_READ_LOCK);
}


int spi_mxic_protect_mode_check(void)
{
	unsigned int val, ret=1;

	if (!((val=spi_mxic_read_scur())& SPI_SCUR_WPSEL)){
		ret = 0;
		}
	return ret;
}

int spi_mxic_enable_protect_mode_otp(void)
{
	spi_unlock();
	spi_write_cmd(RUBY_SPI_WRITE_PRO_SEL);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "Time Out On Write Operation\n");
		spi_lock();
		return ETIME;
	}
	if (!spi_mxic_protect_mode_check()){
		printk(KERN_ERR "%s check error\n",__FUNCTION__);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}

int spi_mxic_spb_mode_check(void)
{
	unsigned int val, ret=1;

	if (((val=spi_mxic_read_lock())& SPI_LOCK_SPM) ){
		ret = 0;
		}
	return ret;
}

int spi_mxic_enable_spb_mode_otp(void)
{
	spi_unlock();
	spi_write_two(RUBY_SPI_WRITE_LOCK,~SPI_LOCK_SPM);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "Time Out On Write Operation\n");
		spi_lock();
		return ETIME;
	}
	if (!spi_mxic_spb_mode_check()){
		printk(KERN_ERR "%s check error\n",__FUNCTION__);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}

int spi_mxic_read_spb(unsigned int addr)
{
	return spi_read_one_addr(RUBY_SPI_READ_SPB,addr);
}

int spi_mxic_spb_check(unsigned int addr)
{
	unsigned int val, ret=1;
	if (!(val=spi_mxic_read_spb(addr))){
		ret=0;
		}
	return ret;
}

int spi_mxic_write_spb(unsigned int addr)
{
	spi_unlock();
	spi_write_addr(RUBY_SPI_WRITE_SPB,addr);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "Time Out On Write Operation\n");
		spi_lock();
		return ETIME;
	}
	if (!spi_mxic_spb_check(addr)){
		printk(KERN_ERR "%s check error at 0x%x\n",__FUNCTION__, addr);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}

int spi_mxic_erase_allspb(void)
{
	spi_unlock();
	spi_write_cmd(RUBY_SPI_ERASE_SPB);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "Timeut On Write Operation\n");
		spi_lock();
		return ETIME;
	}
	spi_lock();
	return 0;
}

uint32_t spi_mxic_read_spb_lock(void)
{
	return spi_read_one(RUBY_SPI_READ_SPBLOCK);
}


int spi_mxic_spb_lock_check(void)
{
	unsigned int val, ret=1;
	if ((val=spi_mxic_read_spb_lock())){
		ret=0;
		}
	return ret;
}


int spi_mxic_write_spb_lock(void)
{
	spi_unlock();
	spi_write_cmd(RUBY_SPI_WRITE_SPBLOCK);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "%s Time Out On Write Operation\n",__FUNCTION__);
		spi_lock();
		return ETIME;
	}
	if (!spi_mxic_spb_lock_check()){
		printk(KERN_ERR "%s check error\n",__FUNCTION__);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}

uint32_t spi_mxic_read_password(void)
{
	return spi_read_four(RUBY_SPI_READ_PASSWORD);
}

int spi_mxic_write_password(unsigned int value)
{
	unsigned int data;
	spi_unlock();
	spi_write_four_prefix_4null(RUBY_SPI_WRITE_PASWORD,value);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "%s Time Out On Write Operation\n",__FUNCTION__);
		spi_lock();
		return ETIME;
	}
	if ( (data=spi_mxic_read_password())!= value){
		printk(KERN_ERR "%s password check error 0x%x\n",__FUNCTION__, data);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}


int spi_mxic_write_password_unlock(unsigned int value)
{
	spi_unlock();
	spi_write_four_prefix_4null(RUBY_SPI_UNLOCK_PASSWORD,value);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "%s Time Out On Write Operation\n",__FUNCTION__);
		spi_lock();
		return ETIME;
	}
	if (spi_mxic_spb_lock_check()){
		printk(KERN_ERR "%s check error\n",__FUNCTION__);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}

int spi_mxic_password_mode_check(void)
{
	unsigned int val, ret=1;

	if (((val=spi_mxic_read_lock())& SPI_LOCK_PPM) ){
		ret = 0;
		}
	return ret;
}

int spi_mxic_enable_password_mode_otp(void)
{
	spi_unlock();
	spi_write_two(RUBY_SPI_WRITE_LOCK,~SPI_LOCK_PPM);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "%s Time Out On Write Operation\n",__FUNCTION__);
		spi_lock();
		return ETIME;
	}
	if (!spi_mxic_password_mode_check()){
		printk(KERN_ERR "%s check error\n",__FUNCTION__);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}


int spi_mxic_read_dpb(unsigned int addr)
{
	return spi_read_one_addr(RUBY_SPI_READ_DPB,addr);
}

int spi_mxic_dpb_check(unsigned int addr)
{
	unsigned int val, ret=1;
	if (!(val=spi_mxic_read_dpb(addr))){
		ret=0;
		}
	return ret;
}


int spi_write_addr_prefix_1null(unsigned int cmd, unsigned int addr)
{

	uint32_t spi_ctrl_val;
	int spi_cmd;

	if (cmd&RUBY_SPI_BASE_ADDR)
		spi_cmd = cmd;
	else
		spi_cmd = RUBY_SPI_CMD_MASK(cmd) + RUBY_SPI_BASE_ADDR;

	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_FIVE_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(SPI_MEM_ADDR_4B(addr), spi_cmd);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	return 0;

}

int spi_mxic_erase_dpb(unsigned int addr)
{
	unsigned int data;
	spi_unlock();
	spi_write_addr_prefix_1null(RUBY_SPI_WRITE_DPB,addr);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "%s Time Out On Write Operation\n",__FUNCTION__);
		spi_lock();
		return ETIME;
	}
	if ((data=spi_mxic_dpb_check(addr))){
		printk(KERN_ERR "%s check error %x\n",__FUNCTION__,data);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}

int spi_mxic_erase_alldpb(void)
{
	uint32_t spi_ctrl_val;

	spi_unlock();
	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_CMD_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(0, RUBY_SPI_GBLOCK_UNLOCK);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "%s Time Out On Write Operation\n",__FUNCTION__);
		spi_lock();
		return ETIME;
	}
	spi_lock();
	return 0;
}

int spi_mxic_write_alldpb(void)
{
	uint32_t spi_ctrl_val;

	spi_unlock();
	spi_ctrl_val = readl(RUBY_SPI_CONTROL);
	writel(RUBY_SPI_PASS_CMD_MASK(spi_ctrl_val), RUBY_SPI_CONTROL);
	writel(0, RUBY_SPI_GBLOCK_LOCK);
	writel(spi_ctrl_val, RUBY_SPI_CONTROL);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "%s Time Out On Write Operation\n",__FUNCTION__);
		spi_lock();
		return ETIME;
	}
	spi_lock();
	return 0;
}

int spi_mxic_spbotp_mode_check(void)
{
	unsigned int val, ret=1;

	if (((val=spi_mxic_read_lock())& SPI_LOCK_SPMOTP) ){
		ret = 0;
		}
	return ret;
}

int spi_mxic_enable_spbotp_mode_otp(void)
{
	unsigned int val;
	spi_unlock();
	val = ~SPI_LOCK_SPMOTP & ~SPI_LOCK_SPM;
	spi_write_two(RUBY_SPI_WRITE_LOCK,val);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "Time Out On Write Operation\n");
		spi_lock();
		return ETIME;
	}
	if (!spi_mxic_spbotp_mode_check()){
		printk(KERN_ERR "%s check error\n",__FUNCTION__);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}

int spi_micron_nlb_mode_check(void)
{
	return spi_mxic_spb_mode_check();
}

int spi_micron_enable_nlb_mode_otp(void)
{
	return spi_mxic_enable_spb_mode_otp();
}

int spi_micron_read_nlb(unsigned int addr)
{
	return spi_mxic_read_spb(addr);
}

int spi_micron_nlb_check(unsigned int addr)
{
	unsigned int val, ret=1;
	if ((val=spi_mxic_read_spb(addr))){
		ret=0;
		}
	return ret;
}

int spi_micron_write_nlb(unsigned int addr)
{
	spi_unlock();
	spi_write_addr(RUBY_SPI_WRITE_SPB,addr);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "Time Out On Write Operation\n");
		spi_lock();
		return ETIME;
	}
	if (!spi_micron_nlb_check(addr)){
		printk(KERN_ERR "%s check error at 0x%x\n",__FUNCTION__, addr);
		spi_lock();
		return -1;
		}
	spi_lock();
	return 0;
}

int spi_micron_erase_allnlb(void)
{
	spi_unlock();
	spi_write_cmd(RUBY_SPI_ERASE_SPB);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "Timeut On Write Operation\n");
		spi_lock();
		return ETIME;
	}
	spi_lock();
	return 0;
}

uint32_t spi_micron_read_status(void)
{
	return spi_read_one(RUBY_SPI_READ_STATUS);
}


int spi_micron_write_status(unsigned int status)
{
	spi_unlock();
	spi_write_one(RUBY_SPI_WRITE_STATUS,status);
	if ((spi_api_flash_status()) == -1){
		printk(KERN_ERR "Time Out On Write Operation\n");
		spi_lock();
		return ETIME;
	}
	spi_lock();
	return 0;
}

uint32_t spi_micron_read_status_bp(void)
{
	unsigned int status, bp;

	status = spi_micron_read_status();
	bp = SPI_MICRON_STATUS_BP(status);

	return bp;
}

int spi_micron_write_status_bp(unsigned int bp)
{
	unsigned int status;
	int ret = 0;

	status = spi_micron_read_status();
	status &= ~SPI_MICRON_STATUS_BP_MASK;
	status |= SPI_MICRON_BP_STATUS(bp);
	ret = spi_micron_write_status(status);

	return ret;
}

int spi_micron_write_status_bottom(void)
{
	unsigned int status;
	int ret = 0;

	status = spi_micron_read_status();
	status |= SPI_MICRON_STATUS_BOTTON_MASK;
	ret = spi_micron_write_status(status);

	return ret;
}

int spi_micron_read_status_bottom(void)
{
	unsigned int status;
	status = spi_micron_read_status();
	if(status&SPI_MICRON_STATUS_BOTTON_MASK){
		return 1;
		}
	return 0;
}


int spi_micron_enable_status_lock_otp(void)
{
	unsigned int status;
	int ret = 0;

	status = spi_micron_read_status();
	status |= SPI_MICRON_STATUS_LOCK_MASK;
	ret = spi_micron_write_status(status);

	return ret;
}

#ifdef SPI_PROC_DEBUG
#include "emac_lib.h"
#define SPI_REG_PROC_FILE_NAME	"spi_reg"
#define SPI_CMD_MAX_LEN	30

static int spi_reg_rw_proc(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	char cmd[SPI_CMD_MAX_LEN];
	int ret = 0;
	unsigned int spidata;
	int val;
	char cmd_mode[20];

	if (!count)
		return -EINVAL;
	else if (count > (SPI_CMD_MAX_LEN - 1))
		return -EINVAL;
	else if (copy_from_user(cmd, buffer, count))
		return -EINVAL;

	cmd[count] = '\0';

	sscanf(cmd, "%s %x", cmd_mode, &spidata);

	if(!strncmp(cmd_mode,"proe",4)){	//proe, enable protection mode
		ret = spi_mxic_enable_protect_mode_otp();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"proc",4)){	//proc, protection mode check
		val = spi_mxic_protect_mode_check();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"lockr",5)){	//lockr, lock register read
		val = spi_mxic_read_lock();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"secur",5)){	//secur, recurity register read
		val = spi_mxic_read_scur();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"spme",4)){	//spme, enable spb mode
		ret = spi_mxic_enable_spb_mode_otp();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"spmc",4)){	//spmc, spb mode check
		val=spi_mxic_spb_mode_check();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"spmotpe",7)){	//spmotpe, enable spmotp mode
		ret = spi_mxic_enable_spbotp_mode_otp();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"spmotpc",7)){	//spmotpc, spmotp mode check
		val=spi_mxic_spbotp_mode_check();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"spbw",4)){	//spbw address, write (set) sector spb
		ret = spi_mxic_write_spb(spidata);
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"spbr",4)){	//spbr address, read sector spb
		val = spi_mxic_read_spb(spidata);
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"spbc",4)){	//spbc address, sector spb check
		val = spi_mxic_spb_check(spidata);
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"spbae",5)){	//spbae, erase (reset) all spb
		ret = spi_mxic_erase_allspb();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"pww",3)){	//pww "password", write (set) password
		ret = spi_mxic_write_password(spidata);
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"pwr",3)){	//pwr, read password
		val = spi_mxic_read_password();
		printk(KERN_ERR"%08x\n",val);
		}
	else if(!strncmp(cmd_mode,"pwme",4)){	//pwme, enable password mode
		ret = spi_mxic_enable_password_mode_otp();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"pwmc",4)){	//pwmc, password mode check
		val = spi_mxic_password_mode_check();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"pwu",3)){	//pwu "password",  unlock password to reset spb lock
		ret = spi_mxic_write_password_unlock(spidata);
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"spblkw",6)){	//spblkw, write (set) spb lock
		ret = spi_mxic_write_spb_lock();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"spblkr",6)){	//spblkr, read spb lock
		val = spi_mxic_read_spb_lock();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"dpbaw",5)){	//dpbaw, write (set) all dpb
		ret = spi_mxic_write_alldpb();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"dpbae",5)){	//dpbae, erase (reset) all spb
		ret = spi_mxic_erase_alldpb();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"dpbe",4)){	//dpbe address, erase (reset) sector dpb
		ret = spi_mxic_erase_dpb(spidata);
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"dpbr",4)){	//dpbr address, read sector dpb
		val = spi_mxic_read_dpb(spidata);
		printk(KERN_ERR"%2x\n",val);
		}
	else if(!strncmp(cmd_mode,"dpbc",4)){	//dpbc address, sector dpb check
		val = spi_mxic_dpb_check(spidata);
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"nlbme",5)){	//nonve, enable nlb mode
		ret = spi_micron_enable_nlb_mode_otp();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"nlbmc",5)){	//nonve, check nlb mode
		val = spi_micron_nlb_mode_check();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"nlbc",4)){	//nonvc address, sector nlb check
		val = spi_micron_nlb_check(spidata);
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"nlbw",4)){	//nonvw address, write (set) sector nlb
		ret = spi_micron_write_nlb(spidata);
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"nlbr",4)){	//nonvr address, read sector nlb
		val = spi_micron_read_nlb(spidata);
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"nlbae",5)){	//nonvae, erase (reset) all nlb
		ret = spi_micron_erase_allnlb();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"stsr",4)){	//stsr, read status
		val = spi_micron_read_status();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"stsle",5)){	//stsle, enable (set) status lock
		ret = spi_micron_enable_status_lock_otp();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"bpr",3)){	//bpr, read bp[3:0]
		val = spi_micron_read_status_bp();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"bpw",3)){	//bpw, write bp[3:0]
		ret = spi_micron_write_status_bp(spidata);
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else if(!strncmp(cmd_mode,"btr",3)){	//btr, read bottom
		val = spi_micron_read_status_bottom();
		printk(KERN_ERR"%x\n",val);
		}
	else if(!strncmp(cmd_mode,"btw",3)){	//btw, write bottom
		ret = spi_micron_write_status_bottom();
		if (!ret) {
			printk(KERN_ERR"complete\n");
			}
		}
	else {
		printk(KERN_ERR"usage: echo [proe|proc|spme|spmc|pwme|pwmc|spbw|spbr|spbc|spbae|pww|pwr|pwu|spblkr|spblkw|dpbaw|dpbae|dpbe|dpbr|dpbc] [addr|password] > /proc/%s\n", SPI_REG_PROC_FILE_NAME);
		}

	return ret ? ret : count;
}

static void spi_reg_proc_name(char *buf)
{
	sprintf(buf, "%s", SPI_REG_PROC_FILE_NAME);
}

int spi_reg_create_proc(void)
{
	char proc_name[12] = {0};

	spi_reg_proc_name(proc_name);

	struct proc_dir_entry *entry = create_proc_entry(proc_name, 0600, NULL);
	if (!entry) {
		return -ENOMEM;
	}

	entry->write_proc = spi_reg_rw_proc;

	return 0;
}


void spi_reg_remove_proc(void)
{
	char proc_name[12];

	spi_reg_proc_name(proc_name);
	remove_proc_entry(proc_name, NULL);
}
#endif

static int __init spi_flash_driver_init(void)
{
	int ret = 0;

	ret = spi_flash_attach(&g_flash);

	printk(KERN_INFO"%s: SPI flash driver initialized %ssuccessfully!\n", SPI_FLASH_DRIVER, ret ? "un" : "");

#ifdef SPI_PROC_DEBUG
	spi_reg_create_proc();
	printk(KERN_INFO"%s: SPI flash proc %s installed\n", SPI_FLASH_DRIVER, SPI_REG_PROC_FILE_NAME);
#endif
	return ret;
}

static void __exit spi_flash_driver_exit(void)
{
	spi_flash_deattach(g_flash);
}

EXPORT_SYMBOL(spi_flash_status);

module_init(spi_flash_driver_init);
module_exit(spi_flash_driver_exit);
