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

int read_sdhc_writei2s(struct fs_file_t *file, const struct device *dev_i2s, struct k_mem_slab *tx_mem_slab);


LOG_MODULE_REGISTER(main);

int main(void)
{
	if (init_sdhc() != 0)
		return -1;

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

    LOG_INF("WAV format: %d Hz, %d-bit, %d channels, format code %d",
        sample_rate, bits_per_sample, num_channels, audio_format);

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

	/* Trigger the I2S transmission */
	ret = i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) {
		printf("Could not trigger I2S tx\n");
		return ret;
	}

	while ((ret = read_sdhc_writei2s(&wavfile, dev_i2s, &tx_0_mem_slab)) > 0) {
		ret = i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
		if (ret < 0) {
			printf("Could not trigger I2S tx\n");
			return ret;
		}
	}
	if (ret < 0) {
		return 1;
	}

	printf("All I2S blocks written\n");
	deinit_sdhc();

	return 0;
}

int read_sdhc_writei2s(struct fs_file_t *file, const struct device *dev_i2s, struct k_mem_slab *tx_mem_slab) {
	int ret;

	void *tx_block[NUM_BLOCKS/2];
	int read_bytes = 0;
	for (int tx_idx = 0; tx_idx < NUM_BLOCKS/2; tx_idx++) {
		ret = k_mem_slab_alloc(tx_mem_slab, &tx_block[tx_idx], K_FOREVER);
		if (ret < 0) {
			printf("Failed to allocate TX block\n");
			return ret;
		}

		int cur_read_bytes;
		cur_read_bytes = fs_read(file, tx_block[tx_idx], BLOCK_SIZE);
		if (cur_read_bytes == 0) {
			LOG_INF("Reached end of file\n");
			while ((ret = i2s_write(dev_i2s, tx_block[tx_idx], BLOCK_SIZE)) == -EIO);
			if (ret < 0) {
				LOG_ERR("Could not write TX buffer\n");
				return ret;
			}
			break;
		} else if (cur_read_bytes < 0) {
			LOG_ERR("Reading block from SD ran into an error\n");
			return cur_read_bytes;
		}
		read_bytes += cur_read_bytes;

		while ((ret = i2s_write(dev_i2s, tx_block[tx_idx], BLOCK_SIZE)) == -EIO);
		if (ret < 0) {
			LOG_ERR("Could not write TX buffer\n");
			return ret;
		}
	}
	return read_bytes;
} 

