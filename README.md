# abqueue
C based FIFO queue, lock free

[![Build Status](https://travis-ci.com/sunwei/abqueue.svg?branch=master)](https://travis-ci.com/sunwei/abqueue)

## Build

```bash

mkdir build

cd build

cmake ..

make 

```

## Test

```bash

cd build

./abqueue-example

```

## Install

```bash

cd build

valgrind --tool=memcheck --leak-check=full ./abqueue-example

sudo make install

```

## Uninstall

```bash

cd build

sudo make uninstall

```