/*
 * HVE Hardware Video Encoding C library imlementation
 *
 * Copyright 2019-2020 (C) Bartosz Meglicki <meglickib@gmail.com>
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
static struct hve *hve_close_and_return_null(struct hve *h, const char *msg);
static int HVE_ERROR_MSG(const char *msg);

// NULL on error
struct hve *hve_init(const struct hve_config *config)
{
	struct hve *h, zero_hve = {0};
	int err;
	AVCodec* codec = NULL;

	if( ( h = (struct hve*)malloc(sizeof(struct hve))) == NULL )
		return hve_close_and_return_null(NULL, "not enough memory for hve");

	*h = zero_hve; //set all members of dynamically allocated struct to 0 in a portable way

	avcodec_register_all();
	av_log_set_level(AV_LOG_VERBOSE);

	//specified device or NULL / empty string for default
	const char *device = (config->device != NULL && config->device[0] != '\0') ? config->device : NULL;

	if( (err = av_hwdevice_ctx_create(&h->hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, device, NULL, 0) ) < 0)
		return hve_close_and_return_null(h, "failed to create VAAPI device");

	const char *encoder = (config->encoder != NULL && config->encoder[0] != '\0') ? config->encoder : "h264_vaapi";

	if(!(codec = avcodec_find_encoder_by_name(encoder)))
		return hve_close_and_return_null(h, "could not find encoder");

	if(!(h->avctx = avcodec_alloc_context3(codec)))
		return hve_close_and_return_null(h, "unable to alloc codec context");

	h->avctx->width = config->width;
	h->avctx->height = config->height;

	if(config->gop_size) //0 for default, -1 for intra only
		h->avctx->gop_size = (config->gop_size != -1) ? config->gop_size : 0;

	h->avctx->time_base = (AVRational){ 1, config->framerate };
	h->avctx->framerate = (AVRational){ config->framerate, 1 };
	h->avctx->sample_aspect_ratio = (AVRational){ 1, 1 };
	h->avctx->pix_fmt = AV_PIX_FMT_VAAPI;

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
		return hve_close_and_return_null(h, NULL);
	}

	if((err = init_hwframes_context(h, config)) < 0)
		return hve_close_and_return_null(h, "failed to set hwframe context");

	AVDictionary *opts = NULL;

	if(config->qp && av_dict_set_int(&opts, "qp", config->qp, 0) < 0)
		return hve_close_and_return_null(h, "failed to initialize option dictionary (qp)");

	if((err = avcodec_open2(h->avctx, codec, &opts)) < 0)
	{
		av_dict_free(&opts);
		return hve_close_and_return_null(h, "cannot open video encoder codec");
	}

	AVDictionaryEntry *de;

	while( (de = av_dict_get(opts, "", de, AV_DICT_IGNORE_SUFFIX)) )
		fprintf(stderr, "hve: %s codec option not found\n", de->key);

	av_dict_free(&opts);

	if(!(h->sw_frame = av_frame_alloc()))
		return hve_close_and_return_null(h, "av_frame_alloc not enough memory");

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

static struct hve *hve_close_and_return_null(struct hve *h, const char *msg)
{
	if(msg)
		fprintf(stderr, "hve: %s\n", msg);

	hve_close(h);

	return NULL;
}

static int HVE_ERROR_MSG(const char *msg)
{
	fprintf(stderr, "hve: %s\n", msg);
	return HVE_ERROR;
}

static int init_hwframes_context(struct hve* h, const struct hve_config *config)
{
	AVBufferRef* hw_frames_ref;
	AVHWFramesContext* frames_ctx = NULL;
	int err = 0;

	if(!(hw_frames_ref = av_hwframe_ctx_alloc(h->hw_device_ctx)))
		return HVE_ERROR_MSG("failed to create VAAPI frame context");

	frames_ctx = (AVHWFramesContext*)(hw_frames_ref->data);
	frames_ctx->format = AV_PIX_FMT_VAAPI;
	frames_ctx->sw_format = h->sw_pix_fmt;
	frames_ctx->width = config->width;
	frames_ctx->height = config->height;
	frames_ctx->initial_pool_size = 20;

	if((err = av_hwframe_ctx_init(hw_frames_ref)) < 0)
	{
		fprintf(stderr, "hve: failed to initialize VAAPI frame context - \"%s\"\n", av_err2str(err));
		av_buffer_unref(&hw_frames_ref);
		return HVE_ERROR_MSG("hint - make sure you are using supported pixel format");
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
			return HVE_ERROR_MSG("error while flushing encoder");

		return HVE_OK;
	}

	//this just copies a few ints and pointers, not the actual frame data
	memcpy(sw_frame->linesize, frame->linesize, sizeof(frame->linesize));
	memcpy(sw_frame->data, frame->data, sizeof(frame->data));

	if(!(h->hw_frame = av_frame_alloc()))
		return HVE_ERROR_MSG("av_frame_alloc not enough memory");

	if((err = av_hwframe_get_buffer(avctx->hw_frames_ctx, h->hw_frame, 0)) < 0)
		return HVE_ERROR_MSG("av_hwframe_get_buffer error");

	if(!h->hw_frame->hw_frames_ctx)
		return HVE_ERROR_MSG("hw_frame->hw_frames_ctx not enough memory");

	if((err = av_hwframe_transfer_data(h->hw_frame, sw_frame, 0)) < 0)
		return HVE_ERROR_MSG("error while transferring frame data to surface");

	if((err = avcodec_send_frame(avctx, h->hw_frame)) < 0)
		return HVE_ERROR_MSG("send_frame error");

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
