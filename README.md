ghostfs
=======

Steganographic filesystem

A simple filesystem to play with different steganographic techniques.
It will essentially work with media files like picture and music.

http://en.wikipedia.org/wiki/Steganography

Example of a filesystem inside another:<br>
<img src="https://raw.githubusercontent.com/mukadr/ghostfs/master/recursivemetal.png" width=320/>

###Sample Usage
```
# Install fuse development libraries (Ubuntu example)
sudo apt-get install libfuse-dev
```
```
# Clone and build
git clone http://github.com/mukadr/ghostfs.git
cd ghostfs
make
```
```
# Convert audio file to wav (in case you only have mp3 or something else)
# You may have ffmpeg instead of avconv depending on the system
avconv -i audio.mp3 audio.wav
```
```
# Format your audio file with ghostfs
./ghost audio.wav f
```
```
# Create an empty folder to use as mountpoint
mkdir mount
```
```
# Mount
./ghost-fuse audio.wav mount
```
```
# Write stuff to it ...
cd mount
echo "Hello World" > foo.txt
```
```
# Unmount
cd ..
fusermount -u mount
```
