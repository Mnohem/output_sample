/*
 * Copyright 2023 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/iterable_sections.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "block.h"
#include "sdhc.h"

LOG_MODULE_REGISTER(main);

int main(void)
{
	if (init_sdhc() != 0)
		return -1;
	
	printk("Inited SDHC");

	struct fs_file_t wavfile;
	fs_file_t_init(&wavfile);

    const char *wav_path = DISK_MOUNT_PT"/music.wav";

	int ret;
    ret = fs_open(&wavfile, wav_path, FS_O_READ);
    if (ret < 0) {
        LOG_ERR("Failed to open file %s (%d)", wav_path, ret);
        return -1;
    }	

	LOG_INF("Opened %s", wav_path);

	uint8_t header[44];
    ssize_t read_bytes = fs_read(&wavfile, header, sizeof(header));
    if (read_bytes < sizeof(header)) {
        LOG_ERR("Failed to read full WAV header");
        fs_close(&wavfile);
        return -1;
    }

	    if (memcmp(header, "RIFF", 4) != 0 || memcmp(&header[8], "WAVE", 4) != 0) {
        LOG_ERR("Not a valid WAV file");
        fs_close(&wavfile);
        return -1;
    }

    uint16_t audio_format = header[20] | (header[21] << 8);
    uint16_t num_channels = header[22] | (header[23] << 8);
    uint32_t sample_rate  = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint16_t bits_per_sample = header[34] | (header[35] << 8);
	uint16_t frame_size = num_channels * (bits_per_sample / 8);

    LOG_INF("WAV format: %d Hz, %d-bit, %d channels, format code %d, frame_size %d",
        sample_rate, bits_per_sample, num_channels, audio_format, frame_size);

	struct i2s_config i2s_cfg;
	const struct device *dev_i2s = DEVICE_DT_GET(DT_NODELABEL(i2s_tx));

	if (!device_is_ready(dev_i2s)) {
		printf("I2S device not ready\n");
		return -ENODEV;
	}

	/* Configure I2S stream */
	i2s_cfg.word_size = bits_per_sample;
	i2s_cfg.channels = num_channels;
	i2s_cfg.format = I2S_FMT_DATA_FORMAT_I2S;
	i2s_cfg.frame_clk_freq = sample_rate;
	i2s_cfg.block_size = BLOCK_SIZE;
	i2s_cfg.timeout = 2000;
	/* Configure the Transmit port as Master */
	i2s_cfg.options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;
	i2s_cfg.mem_slab = &tx_0_mem_slab;
	ret = i2s_configure(dev_i2s, I2S_DIR_TX, &i2s_cfg);
	if (ret < 0) {
		printf("Failed to configure I2S stream\n");
		return ret;
	}
	printk("i2s READY\n");
	uint8_t *buffer;
	for (int i = 0; i < NUM_BLOCKS; i++) {
		ret = k_mem_slab_alloc(&tx_0_mem_slab,(void **)&buffer, K_FOREVER);
		if (ret < 0) {
			printf("Failed to allocate TX block\n");
			return ret;
		}

		ret = fs_read(&wavfile, buffer, BLOCK_SIZE);

		/* Send first block */
		ret = i2s_write(dev_i2s, buffer, BLOCK_SIZE);
		switch (ret) {
			case -EIO:
				printf("Could not write TX buffer -EIO\n");
				return ret;
			case -EBUSY:
				printf("Could not write TX buffer -EBUSY\n");
				return ret;
			case -EAGAIN:
				printf("Could not write TX buffer -EAGAIN\n");
				goto i2s_start;
			case -ENOMEM:
				printf("Could not write TX buffer -ENOMEM\n");
				return;
			case -EINVAL:
				printf("Could not write TX buffer -EINVAL\n");
				return;
		}
		printk("Preallocated block\n");
	}
i2s_start:
	printk("Sent first block\n");

	/* Trigger the I2S transmission */
	ret = i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) {
		printf("Could not trigger I2S tx\n");
		return ret;
	}
	printk("i2s RUNNING\n");
	ssize_t bytes_read;
	while (1) {
		ret = k_mem_slab_alloc(&tx_0_mem_slab, (void **)&buffer, K_FOREVER);
		if (ret < 0) {
			LOG_ERR("Failed to allocate TX block");
			return ret;
		}

		bytes_read = fs_read(&wavfile, buffer, BLOCK_SIZE);
		if (bytes_read <= 0) {
			k_mem_slab_free(&tx_0_mem_slab, (void **)&buffer);  // optional: free unused block
			break;
		}

		// Optional zero-padding
		if (bytes_read < BLOCK_SIZE) {
			memset(buffer + bytes_read, 0, BLOCK_SIZE - bytes_read);
		}

		while ((ret = i2s_write(dev_i2s, buffer, BLOCK_SIZE)) == -EAGAIN || ret == -EBUSY) {
			k_sleep(K_MSEC(1));
		}
		if (ret < 0) {
			LOG_ERR("i2s_write failed: %d", ret);
			i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			return ret;
		} else if (ret == 0) {
			printk("Wrote block bytes_read %d \n", bytes_read);
		}
	}
	/* Drain TX queue */
	ret = i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	if (ret < 0) {
		printf("Could not trigger I2S tx\n");
		return ret;
	}

	printf("All I2S blocks written\n");
	fs_close(&wavfile);
	deinit_sdhc();

	return 0;
}