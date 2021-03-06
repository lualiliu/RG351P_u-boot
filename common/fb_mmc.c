/*
 * Copyright 2014 Broadcom Corporation.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <config.h>
#include <common.h>
#include <fb_mmc.h>
#include <part.h>
#include <aboot.h>
#include <sparse_format.h>
#include <mmc.h>
#include <fb_fastboot.h>
#include <amlogic/aml_mmc.h>

#ifndef CONFIG_FASTBOOT_GPT_NAME
#define CONFIG_FASTBOOT_GPT_NAME GPT_ENTRY_NAME
#endif

#ifndef CONFIG_FASTBOOT_MBR_NAME
#define CONFIG_FASTBOOT_MBR_NAME "mbr"
#endif


/* The 64 defined bytes plus the '\0' */
struct fb_mmc_sparse {
	block_dev_desc_t	*dev_desc;
};
#ifdef CONFIG_EFI_PARTITION
static int part_get_info_efi_by_name_or_alias(block_dev_desc_t *dev_desc,
		const char *name, disk_partition_t *info)
{
	int ret;

	ret = get_partition_info_efi_by_name(dev_desc, name, info);
	if (ret) {
		/* strlen("fastboot_partition_alias_") + 32(part_name) + 1 */
		char env_alias_name[25 + 32 + 1];
		char *aliased_part_name;

		/* check for alias */
		strcpy(env_alias_name, "fastboot_partition_alias_");
		strncat(env_alias_name, name, 32);
		aliased_part_name = getenv(env_alias_name);
		if (aliased_part_name != NULL)
			ret = get_partition_info_efi_by_name(dev_desc,
					aliased_part_name, info);
	}
	return ret;
}
#endif

static lbaint_t fb_mmc_sparse_write(struct sparse_storage *info,
		lbaint_t blk, lbaint_t blkcnt, const void *buffer)
{
	struct fb_mmc_sparse *sparse = info->priv;
	block_dev_desc_t *dev_desc = sparse->dev_desc;

	return dev_desc->block_write(dev_desc->dev, blk, blkcnt,
				     buffer);
}

static lbaint_t fb_mmc_sparse_reserve(struct sparse_storage *info,
		lbaint_t blk, lbaint_t blkcnt)
{
	return blkcnt;
}

static void write_raw_image(block_dev_desc_t *dev_desc, disk_partition_t *info,
		const char *part_name, void *buffer,
		unsigned int download_bytes)
{
	lbaint_t blkcnt;
	lbaint_t blks;

	/* determine number of blocks to write */
	blkcnt = ((download_bytes + (info->blksz - 1)) & ~(info->blksz - 1));
	blkcnt = blkcnt / info->blksz;

	if (blkcnt > info->size) {
		error("too large for partition: '%s'\n", part_name);
		fastboot_fail("too large for partition");
		return;
	}

	puts("Flashing Raw Image\n");

	blks = dev_desc->block_write(dev_desc->dev, info->start, blkcnt,
				     buffer);
	if (blks != blkcnt) {
		error("failed writing to device %d\n", dev_desc->dev);
		fastboot_fail("failed writing to device");
		return;
	}

	printf("........ wrote " LBAFU " bytes to '%s'\n", blkcnt * info->blksz,
	       part_name);
	fastboot_okay("");
}

/* erase or flash, when buffer is not NULL, it's write */
static void fb_mmc_bootloader_ops(const char *cmd,
			block_dev_desc_t *dev_desc, void *buffer,
			unsigned int bytes)
{
	char *delim = "-";
	char *hwpart;
	int map = 0, ret = 0;
	char *scmd = (char *) cmd;
	char *ops[] = {"erase", "write"};
	int mmc_dev = board_current_mmc();

	hwpart = strchr(scmd, (int)*delim);

    if (!hwpart) {
		map = AML_BL_USER;
	} else if (!strcmp(hwpart, "-boot0")) {
		map = AML_BL_BOOT0;
	} else if (!strcmp(hwpart, "-boot1")) {
		map = AML_BL_BOOT1;
	}
    if (map) {
		if (buffer)
            ret = amlmmc_write_bootloader(mmc_dev, map,
				bytes, buffer);
        else
			ret = amlmmc_erase_bootloader(mmc_dev, map);
		if (ret) {
			error("failed %s %s from device %d", (buffer? ops[1]: ops[0]),
				cmd, dev_desc->dev);
			fastboot_fail("failed bootloader operating to device");
			return;
		}
		printf("........ %s  %s\n", (buffer? ops[1]: ops[0]), cmd);
		fastboot_okay("");
	} else
		fastboot_fail("failed opearting from device");
	return;
}


/**
 * write bootloader on user/boot0/boot1
 * according to bootloader name.
 */
static void fb_mmc_write_bootloader(const char *cmd,
		block_dev_desc_t *dev_desc, void *buffer, unsigned int bytes)
{
	return fb_mmc_bootloader_ops(cmd, dev_desc, buffer, bytes);
}

/**
 * erase bootloader on user/boot0/boot1
 * according to bootloader name.
 */
static void fb_mmc_erase_bootloader(const char *cmd, block_dev_desc_t *dev_desc)
{
	return fb_mmc_bootloader_ops(cmd, dev_desc, NULL, 0);
}

void fb_mmc_flash_write(const char *cmd, void *download_buffer,
			unsigned int download_bytes)
{
	block_dev_desc_t *dev_desc;
	disk_partition_t info;
	int mmc_dev = board_current_mmc();
	int ret = 0;

	dev_desc = get_dev("mmc", mmc_dev);
	if (!dev_desc || dev_desc->type == DEV_TYPE_UNKNOWN) {
		error("invalid mmc device\n");
		fastboot_fail("invalid mmc device");
		return;
	}
#ifdef CONFIG_EFI_PARTITION
	if (dev_desc->part_type == PART_TYPE_EFI) {
		if (strcmp(cmd, CONFIG_FASTBOOT_GPT_NAME) == 0) {
			printf("%s: updating MBR, Primary and Backup GPT(s)\n",
			       __func__);
			if (is_valid_gpt_buf(dev_desc, download_buffer)) {
				printf("%s: invalid GPT - refusing to write to flash\n",
				       __func__);
				fastboot_fail("invalid GPT partition");
				return;
			}
			if (write_mbr_and_gpt_partitions(dev_desc, download_buffer)) {
				printf("%s: writing GPT partitions failed\n", __func__);
				fastboot_fail("writing GPT partitions failed");
				return;
			}
			printf("........ success\n");
			fastboot_okay("");
			return;
		} else if (get_partition_info_efi_by_name(dev_desc, cmd, &info)) {
			error("cannot find partition: '%s'\n", cmd);
			fastboot_fail("cannot find partition");
			return;
		}
	}
#endif

#ifdef CONFIG_AML_PARTITION
	if ((dev_desc->part_type == PART_TYPE_AML)
		|| (dev_desc->part_type == PART_TYPE_DOS)
		|| (!strcmp(cmd, CONFIG_FASTBOOT_MBR_NAME))) {
		if (strcmp(cmd, CONFIG_FASTBOOT_MBR_NAME) == 0) {
			printf("%s: updating MBR\n", __func__);
			ret = emmc_update_mbr(download_buffer);
			if (ret)
				fastboot_fail("fastboot update mbr fail");
			else
				fastboot_okay("");
			return;
		}
		if (get_partition_info_aml_by_name(dev_desc, cmd, &info)) {
			error("cannot find partition: '%s'\n", cmd);
			fastboot_fail("cannot find partition");
			return;
		}
	}
#endif

#ifdef CONFIG_MPT_PARTITION
	if (dev_desc->part_type == PART_TYPE_MPT) {
		ret = get_partition_info_mpt_by_name(dev_desc, cmd, &info);
		if (ret) {
			error("cannot find partition: '%s'\n", cmd);
			fastboot_fail("cannot find partition");
			return;
		}
	}
#endif

	if (strcmp(cmd, "dtb") == 0) {
#ifndef DTB_BIND_KERNEL
		ret = dtb_write(download_buffer);
		if (ret)
			fastboot_fail("fastboot write dtb fail");
		else {
			/* renew partition table @ once*/
			if (renew_partition_tbl(download_buffer))
				fastboot_fail("fastboot write dtb fail");
			fastboot_okay("");
		}
#else
	fastboot_fail("dtb is bind in kernel, return");
#endif
	} else if (!strncmp(cmd, "bootloader", strlen("bootloader"))) {
		fb_mmc_write_bootloader(cmd, dev_desc, download_buffer, download_bytes);
		return;
	} else {
		if (is_sparse_image(download_buffer)) {
			struct fb_mmc_sparse sparse_priv;
			struct sparse_storage sparse;

			sparse_priv.dev_desc = dev_desc;

			sparse.blksz = info.blksz;
			sparse.start = info.start;
			sparse.size = info.size;
			sparse.write = fb_mmc_sparse_write;
			sparse.reserve = fb_mmc_sparse_reserve;

			printf("Flashing sparse image at offset " LBAFU "\n",
			       sparse.start);
			sparse.priv = &sparse_priv;
			write_sparse_image(&sparse, cmd, download_buffer,
					   download_bytes);
		} else
			write_raw_image(dev_desc, &info, cmd, download_buffer,
				download_bytes);
	}
}

void fb_mmc_erase_write(const char *cmd, void *download_buffer)
{
	int ret = 0;
	block_dev_desc_t *dev_desc;
	disk_partition_t info;
	lbaint_t blks, blks_start, blks_size, grp_size;
	int mmc_dev = board_current_mmc();
	struct mmc *mmc = find_mmc_device(mmc_dev);

	if (mmc == NULL) {
		error("invalid mmc device");
		fastboot_fail("invalid mmc device");
		return;
	}

	dev_desc = &mmc->block_dev;
	if (!dev_desc || dev_desc->type == DEV_TYPE_UNKNOWN) {
		error("invalid mmc device");
		fastboot_fail("invalid mmc device");
		return;
	}
#ifdef CONFIG_EFI_PARTITION
	if (dev_desc->part_type == PART_TYPE_EFI)
		ret = part_get_info_efi_by_name_or_alias(dev_desc, cmd, &info);
#endif
#ifdef CONFIG_AML_PARTITION
	if ((dev_desc->part_type == PART_TYPE_AML)
		|| (dev_desc->part_type == PART_TYPE_DOS))
		ret = get_partition_info_aml_by_name(dev_desc, cmd, &info);
#endif
#ifdef CONFIG_MPT_PARTITION
	if (dev_desc->part_type == PART_TYPE_MPT)
		ret = get_partition_info_mpt_by_name(dev_desc, cmd, &info);
#endif
	if (ret) {
		error("cannot find partition: '%s'", cmd);
		fastboot_fail("cannot find partition");
		return;
	}
	/* special operation for bootloader */
	if (!strncmp(cmd, "bootloader", strlen("bootloader"))) {
		fb_mmc_erase_bootloader(cmd, dev_desc);
		return;
	} else {
		/* Align blocks to erase group size to avoid erasing other partitions */
		grp_size = mmc->erase_grp_size;
		blks_start = (info.start + grp_size - 1) & ~(grp_size - 1);
		if (info.size >= grp_size)
			blks_size = (info.size - (blks_start - info.start)) &
					(~(grp_size - 1));
		else
			blks_size = 0;

		printf("Erasing blocks " LBAFU " to " LBAFU " due to alignment\n",
		       blks_start, blks_start + blks_size);

		blks = dev_desc->block_erase(dev_desc->dev, blks_start, blks_size);
		if (blks != 0) {
			error("failed erasing from device %d", dev_desc->dev);
			fastboot_fail("failed erasing from device");
			return;
		}

		printf("........ erased " LBAFU " bytes from '%s'\n",
		       blks_size * info.blksz, cmd);
		fastboot_okay("");

	}
}
