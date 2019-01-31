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
 * @brief Encoder configuration.
 * @see hve_init
 */
struct hve_config
{
	int width; //!< width of the encoded frames
	int height; //!< height of the encoded frames
	int framerate; //!< framerate of the encoded video
	const char *device; //!< NULL or device, e.g. "/dev/dri/renderD128"
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
 *
 * @endcode
 * 
 * @param h pointer to internal library data
 * @param error pointer to error code
 * @return 
 * - AVPacket * pointer to FFMpeg AVPacket, you are mainly interested in data and size members
 * - NULL when no more data is pending, query error parameter to check result (HVE_OK on success)
 * 
 * @see hve_send_frame
 */
AVPacket *hve_receive_packet(struct hve *h, int *error);

/** @}*/

#ifdef __cplusplus
}
#endif

#endif
