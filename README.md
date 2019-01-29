# HVE - Hardware Video Encoder C library

This library wraps hardware video encoding in a simple interface.
There are no performance loses (at the cost of library flexibility).

Currently it supports:
- Intel VAAPI compatible hardware encoders ([Quick Sync Video](https://ark.intel.com/Search/FeatureFilter?productType=processors&QuickSyncVideo=true))
- H.264 encoding standard

## Platforms 

Unix-like operating systems (e.g. Linux).
Tested on Ubuntu 18.04.

## Hardware

Intel VAAPI compatible hardware encoders ([Quick Sync Video](https://ark.intel.com/Search/FeatureFilter?productType=processors&QuickSyncVideo=true))

There is possibility that it will also work with Amd/Nvidia hardware.

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
# get compilers and make
sudo apt-get install build-essential
# get git
sudo apt-get install git
# clone the repository
git clone https://github.com/bmegli/hardware-video-encoder.git

# finally build the library and examples
TO DO
```

## Running 

``` bash
TO DO
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
TO DO /dev/dri/renderD128
```

## Testing

Play result raw H.264 file with FFmpeg:

``` bash
# output goes to output.h264 file 
ffplay output.h264
```

## License

Library is licensed under Mozilla Public License, v. 2.0

This is similiar to LGPL but more permissive:
- you can use it as LGPL in prioprietrary software
- unlike LGPL you may compile it statically with your code

Like in LGPL, if you modify this library, you have to make your changes available.
Making a github fork of the library with your changes satisfies those requirements perfectly.

