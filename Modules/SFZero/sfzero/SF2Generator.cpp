/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#include "SF2Generator.h"

#define SF2GeneratorValue(name, type)                                                                                            \
  {                                                                                                                              \
    #name, sfzero::SF2Generator::type                                                                                            \
  }

static const sfzero::SF2Generator generators[] = {

#include "sf2-chunks/generators.h"

};

#undef SF2GeneratorValue

const sfzero::SF2Generator *sfzero::GeneratorFor(int index)
{
  static const int numGenerators = sizeof(generators) / sizeof(generators[0]);

  if (index >= numGenerators)
  {
    return nullptr;
  }
  return &generators[index];
}
