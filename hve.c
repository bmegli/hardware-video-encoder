/*
 * HVE Hardware Video Encoder C library imlementation
 *
 * Copyright 2019-2021 (C) Bartosz Meglicki <meglickib@gmail.com>
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
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#include <stdio.h> //fprintf
#include <stdlib.h> //malloc

// internal library data passed around by the user
struct hve
{
	enum AVPixelFormat sw_pix_fmt;
	AVBufferRef* hw_device_ctx;
	AVCodecContext* avctx;

	//accelerated scaling related
	AVFilterContext *buffersrc_ctx;
	AVFilterContext *buffersink_ctx;
	AVFilterGraph *filter_graph;

	AVFrame *sw_frame; //software
	AVFrame *hw_frame; //hardware
	AVFrame *fr_frame; //filter
	AVPacket enc_pkt;
};

static struct hve *hve_close_and_return_null(struct hve *h, const char *msg);

static int init_hwframes_context(struct hve* h, const struct hve_config *config);
static int init_hardware_scaling(struct hve *h, const struct hve_config *config);

static int hve_pixel_format_depth( enum AVPixelFormat pix_fmt, int *depth);

static int HVE_ERROR_MSG(const char *msg);
static int HVE_ERROR_MSG_FILTER(AVFilterInOut *ins, AVFilterInOut *outs, const char *msg);

static int hw_upload(struct hve *h);
static int scale_encode(struct hve *h);
static int encode(struct hve *h);

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
	avfilter_register_all();
	av_log_set_level(AV_LOG_VERBOSE);

	//specified device or NULL / empty string for default
	const char *device = (config->device != NULL && config->device[0] != '\0') ? config->device : NULL;

	if( (err = av_hwdevice_ctx_create(&h->hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, device, NULL, 0) ) < 0)
		return hve_close_and_return_null(h, "failed to create CUDA device");

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
	h->avctx->pix_fmt = AV_PIX_FMT_CUDA;

	if(config->profile)
		h->avctx->profile = config->profile;

	h->avctx->max_b_frames = config->max_b_frames;
	h->avctx->bit_rate = config->bit_rate;

	if(config->compression_level)
		h->avctx->compression_level = config->compression_level;

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

	if(config->low_power && av_dict_set_int(&opts, "low_power", config->low_power != 0, 0) < 0)
		return hve_close_and_return_null(h, "failed to initialize option dictionary (low_power)");

	if((err = avcodec_open2(h->avctx, codec, &opts)) < 0)
	{
		av_dict_free(&opts);
		return hve_close_and_return_null(h, "cannot open video encoder codec");
	}

	AVDictionaryEntry *de = NULL;

	while( (de = av_dict_get(opts, "", de, AV_DICT_IGNORE_SUFFIX)) )
		fprintf(stderr, "hve: %s codec option not found\n", de->key);

	av_dict_free(&opts);

	if( (config->input_width  && config->input_width  != config->width) ||
	    (config->input_height && config->input_height != config->height) )
		if(init_hardware_scaling(h, config) < 0)
			return hve_close_and_return_null(h, "failed to initialize hardware scaling");
	//from now on h->filter_graph may be used to check if scaling was requested
	if(h->filter_graph)
		if(!(h->fr_frame = av_frame_alloc()))
			return hve_close_and_return_null(h, "av_frame_alloc not enough memory (filter frame)");

	if(!(h->sw_frame = av_frame_alloc()))
		return hve_close_and_return_null(h, "av_frame_alloc not enough memory (software frame");

	h->sw_frame->width = config->input_width ? config->input_width : config->width;
	h->sw_frame->height = config->input_height ? config->input_height : config->height;
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
	av_frame_free(&h->fr_frame);
	av_frame_free(&h->hw_frame);

	avfilter_graph_free(&h->filter_graph);

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

static int init_hwframes_context(struct hve* h, const struct hve_config *config)
{
	AVBufferRef* hw_frames_ref;
	AVHWFramesContext* frames_ctx = NULL;
	int err = 0, depth;

	if(!(hw_frames_ref = av_hwframe_ctx_alloc(h->hw_device_ctx)))
		return HVE_ERROR_MSG("failed to create VAAPI frame context");

	frames_ctx = (AVHWFramesContext*)(hw_frames_ref->data);
	frames_ctx->format = AV_PIX_FMT_CUDA;

	frames_ctx->width = config->input_width ? config->input_width : config->width;
	frames_ctx->height = config->input_height ? config->input_height : config->height;

	frames_ctx->initial_pool_size = 20;

	// Starting from FFmpeg 4.1, avcodec will not fall back to NV12 automatically
	// when using non 4:2:0 software pixel format not supported by codec.
	// Here, instead of using h->sw_pix_fmt we always fall to P010LE for 10 bit
	// input and NV12 otherwise which may possibly lead to some loss of information
	// on modern hardware supporting 4:2:2 and 4:4:4 chroma subsampling
	// (e.g. HEVC with >= IceLake)
	// See:
	// https://github.com/bmegli/hardware-video-encoder/issues/26

	frames_ctx->sw_format = AV_PIX_FMT_NV12;

	if(hve_pixel_format_depth(h->sw_pix_fmt, &depth) != HVE_OK)
		return HVE_ERROR_MSG("failed to get pixel format depth");

	if(depth == 10)
		frames_ctx->sw_format = AV_PIX_FMT_P010LE;

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

static int init_hardware_scaling(struct hve *h, const struct hve_config *config)
{
	const AVFilter *buffersrc, *buffersink;
	AVFilterInOut *ins, *outs;
	char temp_str[128];
	int err = 0;

	if( !(buffersrc = avfilter_get_by_name("buffer")) )
		return HVE_ERROR_MSG("unable to find filter 'buffer'");

	if( !(buffersink = avfilter_get_by_name("buffersink")) )
		return HVE_ERROR_MSG("unable to find filter 'buffersink'");

	//allocate memory
	ins = avfilter_inout_alloc();
	outs = avfilter_inout_alloc();
	h->filter_graph = avfilter_graph_alloc(); //has to be fred with HVE cleanup

	if (!ins || !outs || !h->filter_graph)
		return HVE_ERROR_MSG_FILTER(ins, outs, "unable to allocate memory for the filter");

	//prepare filter source
	snprintf(temp_str, sizeof(temp_str), "video_size=%dx%d:pix_fmt=%d:time_base=1/%d:pixel_aspect=1/1",
		config->input_width, config->input_height, AV_PIX_FMT_VAAPI, config->framerate);

	if(avfilter_graph_create_filter(&h->buffersrc_ctx, buffersrc, "in", temp_str, NULL, h->filter_graph) < 0)
		return HVE_ERROR_MSG_FILTER(ins, outs, "cannot create buffer source");

	outs->name       = av_strdup("in");
	outs->filter_ctx = h->buffersrc_ctx;
	outs->pad_idx    = 0;
	outs->next       = NULL;

	//initialize buffersrc with hw frames context
	AVBufferSrcParameters *par;

	if (!(par = av_buffersrc_parameters_alloc()) )
		return HVE_ERROR_MSG_FILTER(ins, outs, "unable to allocate memory for the filter (params)");

	par->hw_frames_ctx = h->avctx->hw_frames_ctx;

	err = av_buffersrc_parameters_set(h->buffersrc_ctx, par);
	av_free(par);
	if(err < 0)
		return HVE_ERROR_MSG_FILTER(ins, outs, "unable to initialize buffersrc with hw frames context");

	//prepare filter sink
	if(avfilter_graph_create_filter(&h->buffersink_ctx, buffersink, "out", NULL, NULL, h->filter_graph) < 0)
		return HVE_ERROR_MSG_FILTER(ins, outs, "cannot create buffer sink");

	ins->name       = av_strdup("out");
	ins->filter_ctx = h->buffersink_ctx;
	ins->pad_idx    = 0;
	ins->next       = NULL;

	//the actual description of the graph
	snprintf(temp_str, sizeof(temp_str), "format=vaapi,scale_vaapi=w=%d:h=%d", config->width, config->height);

	if(avfilter_graph_parse_ptr(h->filter_graph, temp_str, &ins, &outs, NULL) < 0)
		return HVE_ERROR_MSG_FILTER(ins, outs, "failed to parse filter graph description");

	for (unsigned i = 0; i < h->filter_graph->nb_filters; i++)
		if( !(h->filter_graph->filters[i]->hw_device_ctx = av_buffer_ref(h->hw_device_ctx)) )
			return HVE_ERROR_MSG_FILTER(ins, outs, "not enough memory to reference hw device ctx by filters");

	if(avfilter_graph_config(h->filter_graph, NULL) < 0)
		return HVE_ERROR_MSG_FILTER(ins, outs, "failed to configure filter graph");

	avfilter_inout_free(&ins);
	avfilter_inout_free(&outs);

	return HVE_OK;
}

static int hve_pixel_format_depth(enum AVPixelFormat pix_fmt, int *depth)
{
	const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
	int i;

	if (!desc || !desc->nb_components)
		return HVE_ERROR;

	*depth = -INT_MAX;

	for (i = 0; i < desc->nb_components; i++)
		*depth = FFMAX(desc->comp[i].depth, *depth);

	return HVE_OK;
}

static int HVE_ERROR_MSG(const char *msg)
{
	fprintf(stderr, "hve: %s\n", msg);
	return HVE_ERROR;
}

static int HVE_ERROR_MSG_FILTER(AVFilterInOut *ins, AVFilterInOut *outs, const char *msg)
{
	avfilter_inout_free(&ins);
	avfilter_inout_free(&outs);
	//h->filter_graph is fred in reaction to init_hardware_scaling HVE_ERROR return

	return HVE_ERROR_MSG(msg);
}

int hve_send_frame(struct hve *h,struct hve_frame *frame)
{
	//note - in case hardware frame preparation fails, the frame is fred:
	// - here (this is next user try)
	// - or in av_close (this is user decision to terminate)
	av_frame_free(&h->hw_frame);

	// NULL frame is used for flushing the encoder
	if(frame == NULL)
	{
		if(h->filter_graph)
			if(av_buffersrc_add_frame_flags(h->buffersrc_ctx, NULL, AV_BUFFERSRC_FLAG_KEEP_REF | AV_BUFFERSRC_FLAG_PUSH))
				fprintf(stderr, "hve: error while marking filter EOF\n");

		if (avcodec_send_frame(h->avctx, NULL)  < 0)
			return HVE_ERROR_MSG("error while flushing encoder");

		return HVE_OK;
	}

	//this just copies a few ints and pointers, not the actual frame data
	memcpy(h->sw_frame->linesize, frame->linesize, sizeof(frame->linesize));
	memcpy(h->sw_frame->data, frame->data, sizeof(frame->data));

	if(hw_upload(h) < 0)
		return HVE_ERROR_MSG("failed to upload frame data to hardware");

	if(h->filter_graph)
		return scale_encode(h);

	return encode(h);
}

static int hw_upload(struct hve *h)
{
	if(!(h->hw_frame = av_frame_alloc()))
		return HVE_ERROR_MSG("av_frame_alloc not enough memory for hw_frame");

	if(av_hwframe_get_buffer(h->avctx->hw_frames_ctx, h->hw_frame, 0) < 0)
		return HVE_ERROR_MSG("av_hwframe_get_buffer error");

	if(!h->hw_frame->hw_frames_ctx)
		return HVE_ERROR_MSG("hw_frame->hw_frames_ctx not enough memory");

	if(av_hwframe_transfer_data(h->hw_frame, h->sw_frame, 0) < 0)
		return HVE_ERROR_MSG("error while transferring frame data to surface");

	return HVE_OK;
}

static int scale_encode(struct hve *h)
{
	int err, err2;

	if (av_buffersrc_add_frame_flags(h->buffersrc_ctx, h->hw_frame, AV_BUFFERSRC_FLAG_KEEP_REF | AV_BUFFERSRC_FLAG_PUSH) < 0)
		return HVE_ERROR_MSG("failed to push frame to filtergraph");

	while((err = av_buffersink_get_frame(h->buffersink_ctx, h->fr_frame)) >= 0)
	{
		err2 = avcodec_send_frame(h->avctx, h->fr_frame);
		av_frame_unref(h->fr_frame);

		if(err2 < 0)
			return HVE_ERROR_MSG("send_frame error (after scaling)");
	}

	if(err == AVERROR(EAGAIN) || err == AVERROR_EOF)
		return HVE_OK;

	if(err < 0)
		return HVE_ERROR_MSG("failed to get frame from filtergraph");

	return HVE_OK;
}

static int encode(struct hve *h)
{
	if(avcodec_send_frame(h->avctx, h->hw_frame) < 0)
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
