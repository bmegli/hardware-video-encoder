/*
 * HVE Hardware Video Encoder library example of encoding through VAAPI or NVENC to HEVC 10 bits per channel
 *
 * Copyright 2019-2023 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <stdio.h> //printf, fprintf
#include <inttypes.h> //uint8_t, uint16_t

#include "../hve.h"

const int WIDTH=1280;
const int HEIGHT=720;
const int INPUT_WIDTH=1280; //optional scaling if different from width
const int INPUT_HEIGHT=720; //optional scaling if different from height
const int FRAMERATE=30;
int SECONDS=10;
const char *DEVICE=NULL; //NULL for default or device e.g. "/dev/dri/renderD128"
const char *ENCODER="hevc_vaapi";//NULL for default (h264_vaapi) or FFmpeg encoder e.g. "h264_vaapi", "hevc_vaapi", "h264_nvenc", "hevc_nvenc", ..., ...
const char *PIXEL_FORMAT="p010le"; //NULL for default (nv12) or pixel format e.g. "rgb0", ...
const int PROFILE=FF_PROFILE_HEVC_MAIN_10; //or FF_PROFILE_HEVC_MAIN, ...
const int BFRAMES=0; //max_b_frames, set to 0 to minimize latency, non-zero to minimize size
const int BITRATE=0; //average bitrate in VBR mode (bit_rate != 0 and qp == 0)
const int QP=0; //quantization parameter in CQP mode (qp != 0 and bit_rate == 0)
const int GOP_SIZE=0; //group of pictures size, 0 for default (determines keyframe period)
const int COMPRESSION_LEVEL=0; //encoder/codec dependent, 0 for default, for VAAPI 1-7 speed-quality tradeoff, 1 highest quality, 7 fastest
const int VAAPI_LOW_POWER=0; //alternative VAAPI limited low-power encoding path if non-zero
const char *NVENC_PRESET=NULL; //NVENC and codec specific, NULL / "" or like "default", "slow", "medium", "fast", "hp", "hq", "bd", "ll", "llhq", "llhp", "lossless", "losslesshp"
const int NVENC_DELAY=0; //NVENC specific delay of frame output, 0 for default, -1 for 0 or positive value, set -1 to minimize latency
const int NVENC_ZEROLATENCY=0; //NVENC specific no reordering delay if non-zero, enable to minimize latency
const int NVMPI_NUM_CAPTURE_BUFFERS=0; //NVMPI specific number of buffers in the capture context if non-zero, smaller values may improve latency

int encoding_loop(struct hve *hardware_encoder, FILE *output_file);
int process_user_input(int argc, char* argv[]);
int hint_user_on_failure(char *argv[]);
void hint_user_on_success();

int main(int argc, char* argv[])
{
	//get SECONDS and DEVICE from the command line
	if( process_user_input(argc, argv) < 0 )
		return -1;

	//prepare library data
	struct hve_config hardware_config = {WIDTH, HEIGHT, INPUT_WIDTH, INPUT_HEIGHT, FRAMERATE,
	                                     DEVICE, ENCODER, PIXEL_FORMAT, PROFILE, BFRAMES,
	                                     BITRATE, QP, GOP_SIZE, COMPRESSION_LEVEL,
	                                     VAAPI_LOW_POWER,
	                                     NVENC_PRESET, NVENC_DELAY, NVENC_ZEROLATENCY,
	                                     NVMPI_NUM_CAPTURE_BUFFERS};

	struct hve *hardware_encoder;

	//prepare file for raw HEVC output
	FILE *output_file = fopen("output.hevc", "w+b");
	if(output_file == NULL)
		return fprintf(stderr, "unable to open file for output\n");

	//initialize library with hve_init
	if( (hardware_encoder = hve_init(&hardware_config)) == NULL )
	{
		fclose(output_file);
		return hint_user_on_failure(argv);
	}

	//do the actual encoding
	int status = encoding_loop(hardware_encoder, output_file);

	hve_close(hardware_encoder);
	fclose(output_file);

	if(status == 0)
		hint_user_on_success();

	return 0;
}

int encoding_loop(struct hve *hardware_encoder, FILE *output_file)
{
	struct hve_frame frame = { 0 };
	int frames=SECONDS*FRAMERATE, f, failed, i;

	//we are working with P010LE because we specified p010le pixel format
	//when calling hve_init, in principle we could use other format
	//if hardware supported it (e.g. RGB0 is supported on my Intel)
	uint16_t Y[INPUT_WIDTH*INPUT_HEIGHT]; //dummy p010le luminance data (or p016le)
	uint16_t color[INPUT_WIDTH*INPUT_HEIGHT/2]; //dummy p010le color data (or p016le)

	//fill with your stride (width including padding if any)
	frame.linesize[0] = frame.linesize[1] = INPUT_WIDTH*2;

	//encoded data is returned in FFmpeg packet
	AVPacket *packet;

	for(f=0;f<frames;++f)
	{
		//prepare dummy image data, normally you would take it from camera or other source
		for(int i=0;i<INPUT_WIDTH*INPUT_HEIGHT;++i)
			Y[i] = UINT16_MAX * f / frames; //linear interpolation between 0 and UINT16_MAX
		for(int i=0;i<INPUT_WIDTH*INPUT_HEIGHT/2;++i)
			color[i] = UINT16_MAX / 2; //dummy middle value for U/V, equals 128 << 8, equals 32768
		//fill hve_frame with pointers to your data in P010LE pixel format
		//note that we have actually prepared P016LE data but it is binary compatible with P010LE
		frame.data[0]=(uint8_t*)Y;
		frame.data[1]=(uint8_t*)color;

		//encode this frame
		if( hve_send_frame(hardware_encoder, &frame) != HVE_OK)
			break; //break on error

		while( (packet=hve_receive_packet(hardware_encoder, &failed)) )
		{
			//packet.data is HEVC encoded frame of packet.size length
			//here we are dumping it to raw HEVC file as example
			//yes, we ignore the return value of fwrite for simplicty
			//it could also fail in harsh real world...
			fwrite(packet->data, packet->size, 1, output_file);
		}

		//NULL packet and non-zero failed indicates failure during encoding
		if(failed)
			break; //break on error
	}

	//flush the encoder by sending NULL frame, encode some last frames returned from hardware
	hve_send_frame(hardware_encoder, NULL);
	while( (packet=hve_receive_packet(hardware_encoder, &failed)) )
		fwrite(packet->data, packet->size, 1, output_file);

	//did we encode everything we wanted?
	//convention 0 on success, negative on failure
	return f == frames ? 0 : -1;
}

int process_user_input(int argc, char* argv[])
{
	if(argc < 2)
	{
		fprintf(stderr, "Usage: %s <seconds> [encoder] [device]\n", argv[0]);
		fprintf(stderr, "\nexamples:\n");
		fprintf(stderr, "%s 10\n", argv[0]);
		fprintf(stderr, "%s 10 hevc_vaapi\n", argv[0]);
		fprintf(stderr, "%s 10 hevc_vaapi /dev/dri/renderD128\n", argv[0]);
		fprintf(stderr, "%s 10 hevc_nvenc\n", argv[0]);
		return -1;
	}

	SECONDS = atoi(argv[1]);
	if(argc >= 3) ENCODER = argv[2];
	if(argc >= 4) DEVICE = argv[3];

	return 0;
}

int hint_user_on_failure(char *argv[])
{
	fprintf(stderr, "unable to initalize encoder, try to specify device e.g:\n\n");
	fprintf(stderr, "%s 10 hevc_vaapi /dev/dri/renderD128\n", argv[0]);
	return -1;
}

void hint_user_on_success()
{
	printf("finished successfully\n");
	printf("output written to \"output.hevc\" file\n");
	printf("test with:\n\n");
	printf("ffplay output.hevc\n");
}
