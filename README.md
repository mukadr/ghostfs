ghostfs
=======

Steganographic filesystem

A simple filesystem to play with different steganographic techniques.
It will essentially work with media files like picture and music.

http://en.wikipedia.org/wiki/Steganography

Example of a filesystem inside another:<br>
<img src="https://raw.githubusercontent.com/mukadr/ghostfs/master/recursivemetal.png" width=320/>

###Creating a new filesystem
```
$ ghost sample.wav f
```

###Listing filesystem content
```
$ ghost sample.wav ?
```

###Mounting 
```
$ ghost-fuse sample.wav mountpoint
```

###Unmounting
```
$ fusermount -u mountpoint
```
