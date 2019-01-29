#ifndef HVE_H
#define HVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>

// internal library data passed to all functions
struct hve;

// configuration of hardware encoder used for initialization
struct hve_config
{
	int width;
	int height;
	int framerate;
	const char *device; //NULL or device, e.g. "/dev/dri/renderD128"
};

struct hve_frame
{
	uint8_t *data[AV_NUM_DATA_POINTERS];
	int linesize[AV_NUM_DATA_POINTERS];
};

enum hve_retval_enum
{
	HVE_ERROR=-1, //!< error occured with errno set
	HVE_OK=0, //!< succesfull execution
};

struct hve *hve_init(const struct hve_config *config);
void hve_close(struct hve* h);

int hve_send_frame(struct hve *h,struct hve_frame *frame);
AVPacket *hve_receive_packet(struct hve *h, int *error);

#ifdef __cplusplus
}
#endif

#endif
