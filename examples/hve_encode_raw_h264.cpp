/*
 * HVE Hardware Video Encoder library example of encoding through VAAPI to H.264
 *
 * Copyright 2019 (C) Bartosz Meglicki <meglickib@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <stdio.h> //printf, fprintf
#include <inttypes.h> //uint8_t

#include <opencv2/opencv.hpp>
using namespace cv;

#include "../hve.h"

const int WIDTH=640;
const int HEIGHT=480;
const int FRAMERATE=30;
int SECONDS=10;
const char *DEVICE=NULL; //NULL for default or device e.g. "/dev/dri/renderD128"
const char *ENCODER=NULL;//NULL for default (h264_vaapi) or FFmpeg encoder e.g. "hevc_vaapi", ...
const char *PIXEL_FORMAT="nv12"; //NULL for default (NV12) or pixel format e.g. "rgb0"
const int PROFILE=FF_PROFILE_H264_HIGH; //or FF_PROFILE_HEVC_MAIN, FF_PROFILE_H264_CONSTRAINED_BASELINE, ...
const int BFRAMES=0; //max_b_frames, set to 0 to minimize latency, non-zero to minimize size
const int BITRATE=0; //average bitrate in VBR

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
	struct hve_config hardware_config = {WIDTH, HEIGHT, FRAMERATE, DEVICE, ENCODER, PIXEL_FORMAT, PROFILE, BFRAMES, BITRATE};
	struct hve *hardware_encoder;

	//prepare file for raw H.264 output
	FILE *output_file = fopen("output.h264", "w+b");
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
	int frames=SECONDS*FRAMERATE, f, failed;

	//we are working with NV12 because we specified nv12 pixel format
	//when calling hve_init, in principle we could use other format
	//if hardware supported it (e.g. RGB0 is supported on my Intel)

	Mat luminance(HEIGHT, WIDTH,CV_8UC1, Scalar(128));
	Mat uv(HEIGHT/2,WIDTH,CV_8UC1, Scalar(128));

	//fill with your stride (width including padding if any)
	frame.linesize[0] = frame.linesize[1] = luminance.step[0];

	//encoded data is returned in FFmpeg packet
	AVPacket *packet;

	for(f=0;f<frames;++f)
	{
		//prepare dummy image data, normally you would take it from camera or other source
		//memset(Y, f % 255, WIDTH*HEIGHT); //NV12 luminance (ride through greyscale)
		//memset(color, 128, WIDTH*HEIGHT/2); //NV12 UV (no color really)

		//fill hve_frame with pointers to your data in NV12 pixel format
		frame.data[0]=luminance.ptr();
		frame.data[1]=uv.ptr();

		//encode this frame
		if( hve_send_frame(hardware_encoder, &frame) != HVE_OK)
			break; //break on error

		while( (packet=hve_receive_packet(hardware_encoder, &failed)) )
		{
			//packet.data is H.264 encoded frame of packet.size length
			//here we are dumping it to raw H.264 file as example
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
		fprintf(stderr, "Usage: %s <seconds> [device]\n", argv[0]);
		fprintf(stderr, "\nexamples:\n");
		fprintf(stderr, "%s 10\n", argv[0]);
		fprintf(stderr, "%s 10 /dev/dri/renderD128\n", argv[0]);
		return -1;
	}

	SECONDS = atoi(argv[1]);
	DEVICE=argv[2]; //NULL as last argv argument, or device path

	return 0;
}

int hint_user_on_failure(char *argv[])
{
	fprintf(stderr, "unable to initalize encoder, try to specify device e.g:\n\n");
	fprintf(stderr, "%s 10 /dev/dri/renderD128\n", argv[0]);
	return -1;
}
void hint_user_on_success()
{
	printf("finished successfully\n");
	printf("output written to \"outout.h264\" file\n");
	printf("test with:\n\n");
	printf("ffplay output.h264\n");
}
