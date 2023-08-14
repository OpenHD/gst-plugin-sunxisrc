/*
 * Copyright (c) 2014-2015 Jens Kuske <jenskuske@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#ifndef __H264ENC_H__
#define __H264ENC_H__

struct h264enc_params {
	unsigned int width;
	unsigned int height;

	unsigned int src_width;
	unsigned int src_height;

	unsigned int profile_idc, level_idc;

	unsigned int qp;

    unsigned int bitrate;
    
	unsigned int keyframe_interval;
};

typedef struct h264enc_internal h264enc;

h264enc *h264enc_new(const struct h264enc_params *p, int num_buffers);
void h264enc_free(h264enc *c);
void h264enc_set_input_buffer(h264enc *c, void *Dat, size_t Len);
void *h264enc_get_bytestream_buffer(const h264enc *c, int stream);
unsigned int h264enc_get_bytestream_length(const h264enc *c, int stream);
void h264enc_done_outputbuffer(h264enc *c);
int h264enc_encode_picture(h264enc *c);
void h264_set_bitrate(unsigned int Kbits);
unsigned char **h264_get_buffers(h264enc *c);

void *h264enc_get_intial_bytestream_buffer(h264enc *c);
int h264enc_get_initial_bytestream_length(h264enc *c);
unsigned int h264enc_is_keyframe(const h264enc *c);

#endif
