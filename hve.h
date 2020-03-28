/*
 * HVE Hardware Video Encoding C library header
 *
 * Copyright 2019 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/**
 ******************************************************************************
 *
 *  \mainpage HVE documentation
 *  \see https://github.com/bmegli/hardware-video-encoder
 *
 *  \copyright  Copyright (C) 2019 Bartosz Meglicki
 *  \file       hve.h
 *  \brief      Library public interface header
 *
 ******************************************************************************
 */

#ifndef HVE_H
#define HVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>

/** \addtogroup interface Public interface
 *  @{
 */

/**
 * @struct hve
 * @brief Internal library data passed around by the user.
 * @see hve_init, hve_close
 */
struct hve;

/**
 * @struct hve_config
 * @brief Encoder configuration
 *
 * The device can be:
 * - NULL or empty string (select automatically)
 * - point to valid device e.g. "/dev/dri/renderD128" for vaapi
 *
 * If you have multiple VAAPI devices (e.g. NVidia GPU + Intel) you may have
 * to specify Intel directly. NVidia will not work through VAAPI for encoding
 * (it works through VAAPI-VDPAU bridge and VDPAU is only for decoding).
 *
 * The encoder can be:
 * - NULL or empty string for "h264_vaapi"
 * - valid ffmpeg encoder
 *
 * You may check encoders supported by your hardware with ffmpeg:
 * @code
 * ffmpeg -encoders | grep vaapi
 * @endcode
 *
 * Encoders typically can be:
 * - h264_vaapi
 * - hevc_vaapi
 * - mjpeg_vaapi
 * - mpeg2_vaapi
 * - vp8_vaapi
 * - vp9_vaapi
 *
 * The pixel_format (format of what you upload) typically can be:
 * - nv12 (this is generally safe choice)
 * - yuv420p
 * - yuyv422
 * - uyvy422
 * - yuv422p
 * - rgb0
 * - bgr0
 * - p010le
 *
 * There are no software color conversions in this library.
 *
 * For pixel format explanation see:
 * <a href="https://ffmpeg.org/doxygen/3.4/pixfmt_8h.html#a9a8e335cf3be472042bc9f0cf80cd4c5">FFmpeg pixel formats</a>
 *
 * The available profiles depend on used encoder. Use 0 to guess from input.
 *
 * For possible profiles see:
 * <a href="https://ffmpeg.org/doxygen/3.4/avcodec_8h.html#ab424d258655424e4b1690e2ab6fcfc66">FFmpeg profiles</a>
 *
 * For H.264 profile can typically be:
 * - FF_PROFILE_H264_CONSTRAINED_BASELINE
 * - FF_PROFILE_H264_MAIN
 * - FF_PROFILE_H264_HIGH
 * - ...
 *
 * For HEVC profile can typically be:
 * - FF_PROFILE_HEVC_MAIN
 * - FF_PROFILE_HEVC_MAIN_10 (10 bit channel precision)
 * - ...
 *
 * You may check profiles supported by your hardware with vainfo:
 * @code
 * vainfo --display drm --device /dev/dri/renderDXYZ
 * @endcode
 *
 * The max_b_frames controls the number of B frames.
 * Disable B frames if you need low latency (at the cost of quality/space).
 * The output will be delayed by max_b_frames+1 relative to the input.
 *
 * The bit_rate is average bitrate in VBR mode.
 *
 * The gop_size is size of group of pictures (e.g. I, P, B frames).
 * Note that gop_size determines keyframe period.
 * Setting gop_size equal to framerate results in one keyframe per second.
 * Use 0 value for default, -1 for intra only.
 *
 * @see hve_init
 */
struct hve_config
{
	int width; //!< width of the encoded frames
	int height; //!< height of the encoded frames
	int framerate; //!< framerate of the encoded video
	const char *device; //!< NULL / "" or device, e.g. "/dev/dri/renderD128"
	const char *encoder; //!< NULL / "" or encoder, e.g. "h264_vaapi"
	const char *pixel_format; //!< NULL / "" for NV12 or format, e.g. "rgb0", "bgr0", "nv12", "yuv420p", "p010le"
	int profile; //!< 0 to guess from input or profile e.g. FF_PROFILE_H264_MAIN, FF_PROFILE_H264_HIGH, FF_PROFILE_HEVC_MAIN, ...
	int max_b_frames; //!< maximum number of B-frames between non-B-frames (disable if you need low latency)
	int bit_rate; //!< the average bitrate in VBR mode
	int gop_size; //!<  group of pictures size, 0 for default, -1 for intra only
};

/**
 * @struct hve_frame
 * @brief Data to be encoded (single frame).
 *
 * Fill linsize array with stride (width and padding) of the data in bytes.
 * Fill data with pointers to the data (no copying is needed).
 *
 * For non planar formats only data[0] and linesize[0] is used.
 *
 * Pass the result to hve_send_frame.
 *
 * @see hve_send_frame
 */
struct hve_frame
{
	uint8_t *data[AV_NUM_DATA_POINTERS]; //!< array of pointers to frame planes (e.g. Y plane and UV plane)
	int linesize[AV_NUM_DATA_POINTERS]; //!< array of strides (width + padding) for planar frame formats
};

/**
  * @brief Constants returned by most of library functions
  */
enum hve_retval_enum
{
	HVE_ERROR=-1, //!< error occured with errno set
	HVE_OK=0, //!< succesfull execution
};

/**
 * @brief initialize internal library data.
 * @param config encoder configuration
 * @return
 * - pointer to internal library data
 * - NULL on error, errors printed to stderr
 *
 * @see hve_config, hve_close
 */
struct hve *hve_init(const struct hve_config *config);

/**
 * @brief free library resources
 *
 * Cleans and frees memory
 *
 * @param h pointer to internal library data
 *
 */
void hve_close(struct hve* h);

/**
 * @brief Send frame to hardware for encoding.
 *
 * Call this for each frame you want to encode.
 * Follow with hve_receive_packet to get encoded data from hardware.
 * Call with NULL frame argument to flush the encoder when you want to finish encoding.
 * After flushing follow with hve_receive_packet to get last encoded frames.
 * After flushing it is not possible to reuse the encoder.
 *
 * The pixel format of the frame should match the one specified in hve_init.
 *
 * Perfomance hints:
 *  - don't copy data from your source, just pass the pointers to data planes
 *
 * @param h pointer to internal library data
 * @param frame data to encode
 * @return
 * - HVE_OK on success
 * - HVE_ERROR indicates error
 *
 * @see hve_frame, hve_receive_packet
 *
 * Example flushing:
 * @code
 *  hve_send_frame(hardware_encoder, NULL);
 *
 *	while( (packet=hve_receive_packet(hardware_encoder, &failed)) )
 *	{
 *		//do something with packet->datag, packet->size
 *	}
 *
 *	//NULL packet and non-zero failed indicates failure during encoding
 *	if(failed)
 *		//your logic on failure
 *
 * @endcode
 *
 */
int hve_send_frame(struct hve *h,struct hve_frame *frame);


/**
 * @brief Retrieve encoded frame data from hardware.
 *
 * Keep calling this functions after hve_send_frame until NULL is returned.
 * The ownership of returned AVPacket remains with the library:
 * - consume it immidiately
 * - or copy the data
 *
 * While beginning encoding you may have to send a few frames before you will get packets.
 * When flushing the encoder you may get multiple packets afterwards.
 *
 * @param h pointer to internal library data
 * @param error pointer to error code
 * @return
 * - AVPacket * pointer to FFMpeg AVPacket, you are mainly interested in data and size members
 * - NULL when no more data is pending, query error parameter to check result (HVE_OK on success)
 *
 * @see hve_send_frame
 *
 * Example (in encoding loop):
 * @code
 *  if( hve_send_frame(hardware_encoder, &frame) != HVE_OK)
 *		break; //break on error
 *
 *	while( (packet=hve_receive_packet(hardware_encoder, &failed)) )
 *	{
 *		//do something with packet->data, packet->size
 *	}
 *
 *	//NULL packet and non-zero failed indicates failure during encoding
 *	if(failed)
 *		break; //break on error
 * @endcode
 *
 */
AVPacket *hve_receive_packet(struct hve *h, int *error);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif
