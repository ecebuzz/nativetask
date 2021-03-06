/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "commons.h"
#include "NativeTask.h"
#include "Lz4Codec.h"

extern "C" {
extern int LZ4_compress   (char* source, char* dest, int isize);
extern int LZ4_uncompress (char* source, char* dest, int osize);

/*
LZ4_compress() :
 return : the number of bytes in compressed buffer dest
 note : destination buffer must be already allocated.
  To avoid any problem, size it to handle worst cases situations (input data not compressible)
  Worst case size is : "inputsize + 0.4%", with "0.4%" being at least 8 bytes.

LZ4_uncompress() :
 osize  : is the output size, therefore the original size
 return : the number of bytes read in the source buffer
    If the source stream is malformed, the function will stop decoding and return a negative result, indicating the byte position of the faulty instruction
    This version never writes beyond dest + osize, and is therefore protected against malicious data packets
 note 2 : destination buffer must be already allocated
*/
}

namespace NativeTask {

static int32_t LZ4_MaxCompressedSize(int32_t orig) {
  return std::max((int32_t)(orig*1.005), orig+8);
}

Lz4CompressStream::Lz4CompressStream(
    OutputStream * stream,
    uint32_t bufferSizeHint) :
    BlockCompressStream(stream, bufferSizeHint) {
}

void Lz4CompressStream::compressOneBlock(const void * buff, uint32_t length) {
  size_t compressedLength = _tempBufferSize - 8;
  int ret = LZ4_compress((char*)buff, _tempBuffer + 8, length);
  if (ret > 0) {
    compressedLength = ret;
    ((uint32_t*) _tempBuffer)[0] = bswap(length);
    ((uint32_t*) _tempBuffer)[1] = bswap((uint32_t) compressedLength);
    _stream->write(_tempBuffer, compressedLength + 8);
    _compressedBytesWritten += (compressedLength + 8);
  } else {
    THROW_EXCEPTION(IOException, "compress LZ4 failed");
  }
}

uint64_t Lz4CompressStream::maxCompressedLength(uint64_t origLength) {
  return LZ4_MaxCompressedSize(origLength);
}

//////////////////////////////////////////////////////////////

Lz4DecompressStream::Lz4DecompressStream(
    InputStream * stream,
    uint32_t bufferSizeHint) :
    BlockDecompressStream(stream, bufferSizeHint) {
}

uint32_t Lz4DecompressStream::decompressOneBlock(uint32_t compressedSize,
                                                    void * buff,
                                                    uint32_t length) {
  if (compressedSize > _tempBufferSize) {
    char * newBuffer = (char *)realloc(_tempBuffer, compressedSize);
    if (newBuffer == NULL) {
      THROW_EXCEPTION(OutOfMemoryException, "realloc failed");
    }
    _tempBuffer = newBuffer;
    _tempBufferSize = compressedSize;
  }
  int32_t rd = _stream->readFully(_tempBuffer, compressedSize);
  if (rd != compressedSize) {
    THROW_EXCEPTION(IOException, "readFully reach EOF");
  }
  _compressedBytesRead += rd;
  int ret = LZ4_uncompress(_tempBuffer, (char*)buff, length);
  if (ret == compressedSize) {
    return length;
  } else {
    THROW_EXCEPTION(IOException, "decompress LZ4 failed");
  }
}

uint64_t Lz4DecompressStream::maxCompressedLength(uint64_t origLength) {
  return LZ4_MaxCompressedSize(origLength);
}

} // namespace NativeTask
