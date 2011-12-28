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

#include "BufferStream.h"
#include "FileSystem.h"
#include "Compressions.h"
#include "test_commons.h"


void TestCodec(const string & codec, const string & data, char * buff,
               char * buff2, size_t buffLen, uint32_t buffhint) {
  Timer timer;
  OutputBuffer outputBuffer = OutputBuffer(buff, buffLen);
  CompressStream * compressor =
      Compressions::getCompressionStream(
          codec,
          &outputBuffer,
          buffhint);

  LOG("%s", codec.c_str());
  timer.reset();
  for (size_t i=0;i<data.length();i+=128*1024) {
    compressor->write(data.c_str()+i, std::min(data.length()-i, (size_t)(128*1024)));
  }
  compressor->flush();
  LOG("%s", timer.getSpeedM2("compress origin/compressed", data.length(), outputBuffer.tell()).c_str());

  InputBuffer decompInputBuffer = InputBuffer(buff, outputBuffer.tell());
  DecompressStream * decompressor =
      Compressions::getDecompressionStream(
          codec,
          &decompInputBuffer,
          buffhint);
  size_t total = 0;
  timer.reset();
  while (true) {
    int32_t rd = decompressor->read(buff2+total, buffLen-total);
    if (rd <= 0) {
      break;
    }
    total += rd;
  }
  LOG("%s", timer.getSpeedM2("decompress orig/uncompressed", outputBuffer.tell(), total).c_str());
  LOG("ratio: %.3lf", outputBuffer.tell()/(double)total);
  ASSERT_EQ(data.length(), total);
  ASSERT_EQ(0, memcmp(data.c_str(), buff2, total));

  delete compressor;
  delete decompressor;
}

TEST(Perf, Compressions) {
  string data;
  size_t length = TestConfig.getInt("compression.input.length", 100*1024*1024);
  uint32_t buffhint = TestConfig.getInt("compression.buffer.hint", 128*1024);
  string type = TestConfig.get("compression.input.type", "tera");
  Timer timer;
  GenerateKVTextLength(data, length, type);
  LOG("%s", timer.getInterval("Generate data").c_str());

  InputBuffer inputBuffer = InputBuffer(data);
  size_t buffLen = data.length()/2*3;

  timer.reset();
  char * buff = new char[buffLen];
  char * buff2 = new char[buffLen];
  memset(buff, 0, buffLen);
  memset(buff2, 0, buffLen);
  LOG("%s", timer.getInterval("memset buffer to prevent missing page").c_str());

  TestCodec("org.apache.hadoop.io.compress.SnappyCodec", data, buff, buff2, buffLen, buffhint);
  TestCodec("org.apache.hadoop.io.compress.Lz4Codec", data, buff, buff2, buffLen, buffhint);
  TestCodec("org.apache.hadoop.io.compress.GzipCodec", data, buff, buff2, buffLen, buffhint);

  delete [] buff;
  delete [] buff2;
}

TEST(Perf, CompressionUtil) {
  string inputfile = TestConfig.get("input","");
  string outputfile = TestConfig.get("output","");
  uint32_t buffhint = TestConfig.getInt("compression.buffer.hint", 128*1024);
  string inputcodec = Compressions::getCodecByFile(inputfile);
  string outputcodec = Compressions::getCodecByFile(outputfile);
  size_t bufferSize = buffhint;
  if (inputcodec.length()>0 && outputcodec.length()==0) {
    // decompression
    InputStream * fin = FileSystem::getRaw().open(inputfile);
    if (fin == NULL) {
      THROW_EXCEPTION(IOException, "input file not found");
    }
    DecompressStream * source = Compressions::getDecompressionStream(inputcodec, fin, bufferSize);
    OutputStream * fout = FileSystem::getRaw().create(outputfile, true);
    char * buffer = new char[bufferSize];
    while (true) {
      int rd = source->read(buffer, bufferSize);
      if (rd<=0) {
        break;
      }
      fout->write(buffer, rd);
    }
    source->close();
    delete source;
    fin->close();
    delete fin;
    fout->flush();
    fout->close();
    delete fout;
    delete buffer;
  } else if (inputcodec.length()==0 && outputcodec.length()>0) {
    // compression
    InputStream * fin = FileSystem::getRaw().open(inputfile);
    if (fin == NULL) {
      THROW_EXCEPTION(IOException, "input file not found");
    }
    OutputStream * fout = FileSystem::getRaw().create(outputfile, true);
    CompressStream * dest = Compressions::getCompressionStream(outputcodec, fout, bufferSize);
    char * buffer = new char[bufferSize];
    while (true) {
      int rd = fin->read(buffer, bufferSize);
      if (rd<=0) {
        break;
      }
      dest->write(buffer, rd);
    }
    dest->flush();
    dest->close();
    delete dest;
    fout->close();
    delete fout;
    fin->close();
    delete fin;
    delete buffer;
  } else {
    LOG("Not compression or decompression, do nothing");
  }
}