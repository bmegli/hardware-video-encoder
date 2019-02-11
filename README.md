# HVE - Hardware Video Encoder C library

This library wraps hardware video encoding in a simple interface.
There are no performance loses (at the cost of library flexibility).

Currently it supports VAAPI and H.264 standard.

See library [documentation](https://bmegli.github.io/hardware-video-encoder/group__interface.html).

See also twin [HVD](https://github.com/bmegli/hardware-video-decoder) Hardware Video Decoder library.

## Intended Use

Raw H.264 encoding:
- custom network streaming protocols
- low latency streaming
- raw H.264 dumping
- ...

Complex pipelines (muxing, scaling, color conversions, filtering) are beyond the scope of this library.

## Platforms 

Unix-like operating systems (e.g. Linux).
Tested on Ubuntu 18.04.

## Hardware

Intel VAAPI compatible hardware encoders ([Quick Sync Video](https://ark.intel.com/Search/FeatureFilter?productType=processors&QuickSyncVideo=true))

## Dependencies

Library depends on:
- FFmpeg `avcodec` and `avutil` (at least 3.4 version)

Works with system FFmpeg on Ubuntu 18.04 and doesn't on 16.04 (outdated FFmpeg).

## Building Instructions

Tested on Ubuntu 18.04.

``` bash
# update package repositories
sudo apt-get update 
# get avcodec and avutil (and ffmpeg for testing)
sudo apt-get install ffmpeg libavcodec-dev libavutil-dev
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

## Running Example

``` bash
# ./hve-encode-raw-h264 <number-of-seconds> [device]
./hve-encode-raw-h264 10
```

### Troubleshooting

If you have multiple VAAPI devices you may have to specify Intel directly.

Check with:
```bash
sudo apt-get install vainfo
# try the devices you have in /dev/dri/ path
vainfo --display drm --device /dev/dri/renderD128
```

Once you identify your Intel device run the example, e.g.

```bash
./hve-encode-raw-h264 10 /dev/dri/renderD128
```

## Testing

Play result raw H.264 file with FFmpeg:

``` bash
# output goes to output.h264 file 
ffplay output.h264
```

You should see procedurally generated video (moving through greyscale).

## Using

See examples directory for a more complete and commented examples with error handling.

There are just 4 functions and 3 user-visible data types:
- `hve_init`
- `hve_send_frame` (sends uncompressed data to hardware)
- `hve_receive_packet` (retrieves compressed data from hardware)
- `hve_close`

```C
	struct hve_config hardware_config = {WIDTH, HEIGHT, FRAMERATE, DEVICE,
					PIXEL_FORMAT, PROFILE, BFRAMES, BITRATE};
	struct hve *hardware_encoder=hve_init(&hardware_config);
	struct hve_frame frame = { 0 };

	//later assuming PIXEL_FORMAT is "nv12" (you can use something else)

	//fill with your stride (width including padding if any)
	frame.linesize[0] = frame.linesize[1] = WIDTH;
	
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
- add `avcodec` and `avutil` to linked libraries in IDE project configuration

For dynamic linking of HVE and FFmpeg libraries:
- place `hve.h` where compiler can find it (e.g. `make install` for `/usr/local/include/hve.h`)
- place `libhve.so` where linker can find it (e.g. `make install` for `/usr/local/lib/libhve.so`)
- make sure `/usr/local/...` is considered for libraries
- add `hve`, `avcodec` and `avutil` to linked libraries in IDE project configuration
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
target_link_libraries(your-project hve avcodec avutil)
```

For example see [realsense-ir-to-vaapi-h264](https://github.com/bmegli/realsense-ir-to-vaapi-h264)

### Manually

Assuming your `main.c`/`main.cpp` and `hve.h`, `hve.c` are all in the same directory:

C
```bash
gcc main.c hve.c -lavcodec -lavutil -o your-program
```

C++
```bash
gcc -c hve.c
g++ -c main.cpp
g++ hve.o main.o -lavcodec -lavutil -o your program
```

## License

Library is licensed under Mozilla Public License, v. 2.0

This is similiar to LGPL but more permissive:
- you can use it as LGPL in prioprietrary software
- unlike LGPL you may compile it statically with your code

Like in LGPL, if you modify this library, you have to make your changes available.
Making a github fork of the library with your changes satisfies those requirements perfectly.

Since you are linking to FFmpeg libraries. Consider also `avcodec` and `avutil` licensing.

## Additional information

### Library uses

Realsense D400 camera infrared stream to H.264 - [realsense-ir-to-vaapi-h264](https://github.com/bmegli/realsense-ir-to-vaapi-h264)
