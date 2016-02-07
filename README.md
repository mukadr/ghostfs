ghostfs
=======

Steganographic filesystem

A simple filesystem to play with different steganographic techniques.
Currently it only supports non compressed WAVE files under PCM format.

##Build instructions
####Install FUSE
######Linux (Ubuntu)
```
sudo apt-get install libfuse-dev
```
######Mac OS X
Install OSXFUSE: https://osxfuse.github.io/
####Clone and build
```
git clone http://github.com/mukadr/ghostfs.git
cd ghostfs
make
```
##Usage
####Format
```
ghost audio.wav f
```
####Mount
```
ghost-fuse audio.wav folder
```
####Unmount
######Linux
```
fusermount -u folder
```
######Mac OS X
```
umount folder
```
