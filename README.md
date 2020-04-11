# HVE - Hardware Video Encoder C library

This library wraps hardware video encoding and scaling in a simple interface.
There are no performance loses (at the cost of library flexibility).

Currently it supports VAAPI and various codecs (H.264, HEVC, ...).\
VBR and CQP modes are supported (e.g. streaming and later editting).

See library [documentation](https://bmegli.github.io/hardware-video-encoder/group__interface.html).

See also twin [HVD](https://github.com/bmegli/hardware-video-decoder) Hardware Video Decoder library.

See [hardware-video-streaming](https://github.com/bmegli/hardware-video-streaming) for other related projects.

## Intended Use

Raw encoding (H264, HEVC, ...):
- custom network streaming protocols
- low latency streaming
- raw dumping (H264, HEVC, ...)
- ...

Complex pipelines (muxing, filtering) are beyond the scope of this library.

## Platforms 

Unix-like operating systems (e.g. Linux).
Tested on Ubuntu 18.04.

## Hardware

Intel VAAPI compatible hardware encoders ([Quick Sync Video](https://ark.intel.com/Search/FeatureFilter?productType=processors&QuickSyncVideo=true))

## Dependencies

Library depends on:
- FFmpeg `avcodec`, `avutil`, `avfilter` (at least 3.4 version)

Works with system FFmpeg on Ubuntu 18.04 and doesn't on 16.04 (outdated FFmpeg and VAAPI ecosystem).

## Building Instructions

Tested on Ubuntu 18.04.

``` bash
# update package repositories
sudo apt-get update 
# get avcodec and avutil (and ffmpeg for testing)
sudo apt-get install ffmpeg libavcodec-dev libavutil-dev libavfilter-dev
# get compilers and make and cmake
sudo apt-get install build-essential
# get cmake - we need to specify libcurl4 for Ubuntu 18.04 dependencies problem
sudo apt-get install libcurl4 cmake
# get git
sudo apt-get install git
# clone the repository
git clone https://github.com/bmegli/hardware-video-encoder.git

# finally build the library and examples
cd hardware-video-encoder
mkdir build
cd build
cmake ..
make
```

## Running Examples

``` bash
# ./hve-encode-raw-h264 <number-of-seconds> [device]
./hve-encode-raw-h264 10
```

``` bash
# ./hve-encode-raw-hevc10 <number-of-seconds> [device]
./hve-encode-raw-hevc10 10
```

### Troubleshooting

If you have multiple VAAPI devices you may have to specify Intel directly.

Check with:
```bash
sudo apt-get install vainfo
# try the devices you have in /dev/dri/ path
vainfo --display drm --device /dev/dri/renderD128
```

Once you identify your Intel device run the examples, e.g.

```bash
./hve-encode-raw-h264 10 /dev/dri/renderD128
./hve-encode-raw-hevc10 10 /dev/dri/renderD128
```

Some devices are supported by more than one driver. Functionality differs.

```bash
# list the drivers (typical location)
ls /usr/lib/x86_64-linux-gnu/dri/
```

You may force the driver to use, e.g.

```
# in Ubuntu 20.04 there are two drivers supporting Intel between gen8+ and gen9.5
# functionality differs
# e.g. HEVC encoding support comes later with open source build of iHD (generation wise)
LIBVA_DRIVER_NAME=i965 vainfo --display drm --device /dev/dri/renderD128
LIBVA_DRIVER_NAME=iHD vainfo --display drm --device /dev/dri/renderD128
```

With support depending on hardware, driver (type and version), libva, and FFmpeg the situation is complex.

For example there are two versions of LattePanda Alpha (KabyLake m3-7Y30 and AmberLake Y m3-8100Y):
- m3-7Y30 by default works in Ubuntu 18.04
- m3-8100Y by default doesn't in Ubuntu 18.04 
- m3-8100Y by default works in Ubuntu 20.04 with iHD driver and without HEVC encoding
- m3-8100Y with forced i965 driver works in Ubuntu 20.04 with HEVC and HEVC encoding

So for m3-8100Y and HEVC encoding in Ubuntu 20.04 you would force the driver

```bash
LIBVA_DRIVER_NAME=i965 ./hve-encode-raw-hevc10 10 /dev/dri/renderD128
```

## Testing

Play result raw H.264/HEVC file with FFmpeg:

``` bash
# output goes to output.h264/output.hevc file
ffplay output.h264
ffplay output.hevc
```

You should see procedurally generated video (moving through greyscale).

## Using

See examples directory for more complete and commented examples with error handling.

There are just 4 functions and 3 user-visible data types:
- `hve_init`
- `hve_send_frame` (sends uncompressed data to hardware)
- `hve_receive_packet` (retrieves compressed data from hardware)
- `hve_close`

```C
	struct hve_config hardware_config = {WIDTH, HEIGHT, INPUT_WIDTH, INPUT_HEIGHT, FRAMERATE,
		DEVICE, ENCODER, PIXEL_FORMAT, PROFILE, BFRAMES, BITRATE, QP, GOP_SIZE, COMPRESSION_LEVEL};
	struct hve *hardware_encoder=hve_init(&hardware_config);
	struct hve_frame frame = { 0 };

	//later assuming PIXEL_FORMAT is "nv12" (you may use something else)

	//fill with your stride (width including padding if any)
	frame.linesize[0] = frame.linesize[1] = INPUT_WIDTH;
	
	AVPacket *packet; //encoded data is returned in FFmpeg packet
	int failed; //error indicator while encoding

	//...
	//whatever logic you have to prepare data source
	//...

	while(keep_encoding) 
	{
		//...
		//update NV12 Y and color data (e.g. get them from camera)
		//...
		//fill hve_frame with pointers to your data in NV12 pixel format
		frame.data[0]=Y; //dummy luminance plane
		frame.data[1]=color; //dummy UV color plane
		//encode this frame
		hve_send_frame(hardware_encoder, &frame);
		while( (packet=hve_receive_packet(hardware_encoder, &failed)) )
		{
			//... 
			//packet.data is h.264 encoded frame of packet.size length
			//... so do something with it?
		}
	}
	
	//flush the encoder by sending NULL frame
	hve_send_frame(hardware_encoder, NULL);
	while( (packet=hve_receive_packet(hardware_encoder, &failed)) )
		; //ignore last packets
	
	hve_close(hardware_encoder);
```

That's it! You have just seen all the functions and data types in the library.

## Compiling your code

You have several options.

### IDE (recommended)

For static linking of HVE and dynamic linking of FFmpeg libraries (easiest):
- copy `hve.h` and `hve.c` to your project and add them in your favourite IDE
- add `avcodec`, `avutil`, `avfilter` to linked libraries in IDE project configuration

For dynamic linking of HVE and FFmpeg libraries:
- place `hve.h` where compiler can find it (e.g. `make install` for `/usr/local/include/hve.h`)
- place `libhve.so` where linker can find it (e.g. `make install` for `/usr/local/lib/libhve.so`)
- make sure `/usr/local/...` is considered for libraries
- add `hve`, `avcodec`, `avutil`, `avfilter` to linked libraries in IDE project configuration
- make sure `libhve.so` is reachable to you program at runtime (e.g. set `LD_LIBRARIES_PATH`)

### CMake

Assuming directory structure with HVE as `hardware-video-encoder` subdirectory (or git submodule) of your project.

```
your-project
│   main.cpp
│   CMakeLists.txt
│
└───hardware-video-encoder
│   │   hve.h
│   │   hve.c
│   │   CMakeLists.txt
```

You may use the following top level CMakeLists.txt

``` CMake
cmake_minimum_required(VERSION 3.0)

project(
    your-project
)

# drop the SHARED if you would rather link with HVE statically
add_library(hve SHARED hardware-video-encoder/hve.c)

add_executable(your-project main.cpp)
target_include_directories(your-project PRIVATE hardware-video-encoder)
target_link_libraries(your-project hve avcodec avutil avfilter)
```

For example see [realsense-ir-to-vaapi-h264](https://github.com/bmegli/realsense-ir-to-vaapi-h264)

### Manually

Assuming your `main.c`/`main.cpp` and `hve.h`, `hve.c` are all in the same directory:

C
```bash
gcc main.c hve.c -lavcodec -lavutil -lavfilter -o your-program
```

C++
```bash
gcc -c hve.c
g++ -c main.cpp
g++ hve.o main.o -lavcodec -lavutil -lavfilter -o your program
```

## License

Library is licensed under Mozilla Public License, v. 2.0

This is similiar to LGPL but more permissive:
- you can use it as LGPL in prioprietrary software
- unlike LGPL you may compile it statically with your code

Like in LGPL, if you modify this library, you have to make your changes available.
Making a github fork of the library with your changes satisfies those requirements perfectly.

Since you are linking to FFmpeg libraries consider also `avcodec`, `avutil` and `avfilter` licensing.

## Additional information

### Library uses

Realsense D400 camera infrared stream to H.264 - [realsense-ir-to-vaapi-h264](https://github.com/bmegli/realsense-ir-to-vaapi-h264)\
Realsense D400 camera depth stream to HEVC Main10 - [realsense-depth-to-vaapi-hevc10](https://github.com/bmegli/realsense-depth-to-vaapi-hevc10)

