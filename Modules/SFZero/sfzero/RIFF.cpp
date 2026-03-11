/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#include "RIFF.h"

void sfzero::RIFFChunk::readFrom(juce::InputStream *file)
{
  file->read(&id, sizeof(sfzero::fourcc));
  size = static_cast<sfzero::dword>(file->readInt());
  start = file->getPosition();

  if (FourCCEquals(id, "RIFF"))
  {
    type = RIFF;
    file->read(&id, sizeof(sfzero::fourcc));
    start += sizeof(sfzero::fourcc);
    size -= sizeof(sfzero::fourcc);
  }
  else if (FourCCEquals(id, "LIST"))
  {
    type = LIST;
    file->read(&id, sizeof(sfzero::fourcc));
    start += sizeof(sfzero::fourcc);
    size -= sizeof(sfzero::fourcc);
  }
  else
  {
    type = Custom;
  }
}

void sfzero::RIFFChunk::seek(juce::InputStream *file) { file->setPosition(start); }
void sfzero::RIFFChunk::seekAfter(juce::InputStream *file)
{
  juce::int64 next = start + size;

  if (next % 2 != 0)
  {
    next += 1;
  }
  file->setPosition(next);
}

juce::String sfzero::RIFFChunk::readString(juce::InputStream *file)
{
  juce::MemoryBlock memoryBlock(size);
  file->read(memoryBlock.getData(), static_cast<int>(memoryBlock.getSize()));
  return memoryBlock.toString();
}
