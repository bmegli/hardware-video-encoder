/*
 * HVE Hardware Video Encoding C library imlementation
 *
 * Copyright 2019 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "hve.h"

// FFmpeg
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>

#include <stdio.h> //fprintf
#include <stdlib.h> //malloc

// internal library data passed around by the user
struct hve
{
	AVBufferRef* hw_device_ctx;
	enum AVPixelFormat sw_pix_fmt;
	AVCodecContext* avctx;
	AVFrame *sw_frame;
	AVFrame *hw_frame;
	AVPacket enc_pkt;
};

static int init_hwframes_context(struct hve* h, const struct hve_config *config);
static struct hve *hve_close_and_return_null(struct hve *h);

// NULL on error
struct hve *hve_init(const struct hve_config *config)
{
	struct hve *h, zero_hve = {0};
	int err;
	AVCodec* codec = NULL;

	if( ( h = (struct hve*)malloc(sizeof(struct hve))) == NULL )
	{
		fprintf(stderr, "hve: not enough memory for hve\n");
		return NULL;
	}
	*h = zero_hve; //set all members of dynamically allocated struct to 0 in a portable way

	avcodec_register_all();
	av_log_set_level(AV_LOG_VERBOSE);

	//specified device or NULL / empty string for default
	const char *device = (config->device != NULL && config->device[0] != '\0') ? config->device : NULL;

	if( (err = av_hwdevice_ctx_create(&h->hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, device, NULL, 0) ) < 0)
	{
		fprintf(stderr, "hve: failed to create a VAAPI device\n");
		return hve_close_and_return_null(h);
	}
	//ffmpeg -encoders | grep vaapi
	/*
	 * V..... h264_vaapi           H.264/AVC (VAAPI) (codec h264)
	 * V..... hevc_vaapi           H.265/HEVC (VAAPI) (codec hevc)
	 * V..... mjpeg_vaapi          MJPEG (VAAPI) (codec mjpeg)
	 * V..... mpeg2_vaapi          MPEG-2 (VAAPI) (codec mpeg2video)
	 * V..... vp8_vaapi            VP8 (VAAPI) (codec vp8)
	 * V..... vp9_vaapi            VP9 (VAAPI) (codec vp9)
	 */
	if(!(codec = avcodec_find_encoder_by_name("hevc_vaapi")))
	{
		fprintf(stderr, "hve: could not find encoder\n");
		return hve_close_and_return_null(h);
	}

	if(!(h->avctx = avcodec_alloc_context3(codec)))
	{
		fprintf(stderr, "hve: unable to alloc codec context\n");
		return hve_close_and_return_null(h);
	}

	h->avctx->width = config->width;
	h->avctx->height = config->height;
	h->avctx->time_base = (AVRational){ 1, config->framerate };
	h->avctx->framerate = (AVRational){ config->framerate, 1 };
	h->avctx->sample_aspect_ratio = (AVRational){ 1, 1 };
	h->avctx->pix_fmt = AV_PIX_FMT_VAAPI;

	//Profiles
	//https://ffmpeg.org/doxygen/3.4/avcodec_8h.html
	if(config->profile)
		h->avctx->profile = config->profile;
	h->avctx->max_b_frames = config->max_b_frames;
	h->avctx->bit_rate = config->bit_rate;

	//try to find software pixel format that user wants to upload data in
	if(config->pixel_format == NULL || config->pixel_format[0] == '\0')
		h->sw_pix_fmt = AV_PIX_FMT_NV12;
	else if( ( h->sw_pix_fmt = av_get_pix_fmt(config->pixel_format) ) == AV_PIX_FMT_NONE )
	{
		fprintf(stderr, "hve: failed to find pixel format %s\n", config->pixel_format);
		return hve_close_and_return_null(h);
	}

	if((err = init_hwframes_context(h, config)) < 0)
	{
		fprintf(stderr, "hve: failed to set hwframe context\n");
		return hve_close_and_return_null(h);
	}

	if((err = avcodec_open2(h->avctx, codec, NULL)) < 0)
	{
		fprintf(stderr, "hve: cannot open video encoder codec\n");
		return hve_close_and_return_null(h);
	}

	if(!(h->sw_frame = av_frame_alloc()))
	{
		fprintf(stderr, "hve: av_frame_alloc not enough memory\n");
		return hve_close_and_return_null(h);
	}

	h->sw_frame->width = config->width;
	h->sw_frame->height = config->height;
	h->sw_frame->format = h->sw_pix_fmt;

	av_init_packet(&h->enc_pkt);
	h->enc_pkt.data = NULL;
	h->enc_pkt.size = 0;

	return h;
}

void hve_close(struct hve* h)
{
	if(h==NULL)
		return;

	av_packet_unref(&h->enc_pkt);
	av_frame_free(&h->sw_frame);
	av_frame_free(&h->hw_frame);
	avcodec_free_context(&h->avctx);
	av_buffer_unref(&h->hw_device_ctx);

	free(h);
}
static struct hve *hve_close_and_return_null(struct hve *h)
{
	hve_close(h);
	return NULL;
}

static int init_hwframes_context(struct hve* h, const struct hve_config *config)
{
	AVBufferRef* hw_frames_ref;
	AVHWFramesContext* frames_ctx = NULL;
	int err = 0;

	if(!(hw_frames_ref = av_hwframe_ctx_alloc(h->hw_device_ctx)))
	{
		fprintf(stderr, "hve: failed to create VAAPI frame context\n");
		return HVE_ERROR;
	}
	frames_ctx = (AVHWFramesContext*)(hw_frames_ref->data);
	frames_ctx->format = AV_PIX_FMT_VAAPI;
	frames_ctx->sw_format = h->sw_pix_fmt;
	frames_ctx->width = config->width;
	frames_ctx->height = config->height;
	frames_ctx->initial_pool_size = 20;
	if((err = av_hwframe_ctx_init(hw_frames_ref)) < 0)
	{
		fprintf(stderr, "hve: failed to initialize VAAPI frame context - \"%s\"\n", av_err2str(err));
		fprintf(stderr, "hve: hint - make sure you are using supported pixel format\n");
		av_buffer_unref(&hw_frames_ref);
		return HVE_ERROR;
	}
	h->avctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
	if(!h->avctx->hw_frames_ctx)
		err = AVERROR(ENOMEM);

	av_buffer_unref(&hw_frames_ref);
	return err == 0 ? HVE_OK : HVE_ERROR;
}

int hve_send_frame(struct hve *h,struct hve_frame *frame)
{
	AVCodecContext* avctx=h->avctx;
	AVFrame *sw_frame=h->sw_frame;
	int err = 0;

	//note - in case hardware frame preparation fails, the frame is fred:
	// - here (this is next user try)
	// - or in av_close (this is user decision to terminate)
	av_frame_free(&h->hw_frame);

	// NULL frame is used for flushing the encoder
	if(frame == NULL)
	{
		if ( (err = avcodec_send_frame(avctx, NULL) ) < 0)
		{
			fprintf(stderr, "hve: error while flushing encoder\n");
			return HVE_ERROR;
		}
		return HVE_OK;
	}

	//this just copies a few ints and pointers, not the actual frame data
	memcpy(sw_frame->linesize, frame->linesize, sizeof(frame->linesize));
	memcpy(sw_frame->data, frame->data, sizeof(frame->data));

	if(!(h->hw_frame = av_frame_alloc()))
	{
		fprintf(stderr, "hve: av_frame_alloc not enough memory\n");
		return HVE_ERROR;
	}

	if((err = av_hwframe_get_buffer(avctx->hw_frames_ctx, h->hw_frame, 0)) < 0)
	{
		fprintf(stderr, "hve: av_hwframe_get_buffer error\n");
		return HVE_ERROR;
	}

	if(!h->hw_frame->hw_frames_ctx)
	{
		fprintf(stderr, "hve: hw_frame->hw_frames_ctx not enough memory\n");
		return HVE_ERROR;
	}

	if((err = av_hwframe_transfer_data(h->hw_frame, sw_frame, 0)) < 0)
	{
		fprintf(stderr, "hve: error while transferring frame data to surface\n");
		return HVE_ERROR;
	}

	if((err = avcodec_send_frame(avctx, h->hw_frame)) < 0)
	{
		fprintf(stderr, "hve: send_frame error\n");
		return HVE_ERROR;
	}

	return HVE_OK;
}

// returns:
// - non NULL on success
// - NULL and failed == false if more data is needed
// - NULL and failed == true on error
// the ownership of returned AVPacket* remains with the library
AVPacket *hve_receive_packet(struct hve *h, int *error)
{
	//the packed will be unreffed in:
	//- next call to av_receive_packet through avcodec_receive_packet
	//- av_close (user decides to finish in the middle of encoding)
	//whichever happens first
	int ret = avcodec_receive_packet(h->avctx, &h->enc_pkt);

	*error=HVE_OK;

	if(ret == 0)
		return &h->enc_pkt;

	//EAGAIN means that we need to supply more data
	//EOF means that we are flushing the decoder and no more data is pending
	//otherwise we got an error
	*error = ( ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) ? HVE_OK : HVE_ERROR;
	return NULL;
}
